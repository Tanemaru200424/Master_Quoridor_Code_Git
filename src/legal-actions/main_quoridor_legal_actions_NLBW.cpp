// 先手到達可能な配置の合法手数を求め最大合法手数と平均合法手数を求めるために使う
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <cassert>
#include <cstddef>
#include <vector>
#include <stack>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <bitset>
#include <string>
#include <cmath>
#include <iomanip>
#include <stdio.h>
#include <climits>
#include "../common/zdd_quoridor_board_only.hpp"
#include "../common/posi_quoridor_board_only.hpp"

ZDD zdd;
unsigned int table_entry_size = 2U;
unsigned long long int one_leaves_num_LBW = 0ULL; //ZDDのうち合法手数解析対象にするのはL側だけ。読み込むのもLBWファイルのみ

unsigned long long int table_size64 = 0ULL; //uint64を2ビットエントリにした時のゲーム値表のサイズ。
constexpr int nworker = 7; //分割数（特に指定はない。後で調整。）
constexpr int deq_input_size = 1024; //分割したスレッドにデータを渡すサイズ（とりあえずこれ。後で調整。）
constexpr int deq_output_size = 256; //スレッドが返す結果のデータのサイズ（とりあえずこれ。後で調整。）

// main引数で渡す先手後手フェンス組合せ
int L = 0;
int B = 0;
int W = 0;

using namespace std;

//並列計算の同期用。原理は分からなくてもいい。
condition_variable cv_boss;
condition_variable cv_worker;
mutex mtx;

unsigned long long int lreachable = 0ULL, total_black_actions = 0ULL, total_white_actions = 0ULL, black_reachable_num = 0ULL, white_reachable_num = 0ULL; //到達可能配置の最大ID、先手後手合計合法手数、先手後手到達可能配置数
unsigned int max_black_actions = 0, ave_black_actions = 0; // 先手の最大合法手数、平均合法手数
unsigned int max_white_actions = 0, ave_white_actions = 0; // 後手の最大合法手数、平均合法手数
bool flag_worker_quit = false; //仕事が終わったことを表すフラグ

enum { v_unknown = 0, v_black = 1, v_white = 2, v_both = 3}; //表の値

//ファイル命名規則quoridor_reachable_NLBW（盤面サイズ、盤上フェンス、先手手持ち、後手手持ち）
//LBWのみ読み込み
class InTable{
private:
    unsigned char m_buffer;
    unsigned int m_num_keep;
    ifstream m_ifs_LBW;
public:
    InTable() = delete; //引数なしコンストラクタを禁止する。
    //呼び出すコンストラクタは必ずこっち。ZDD構築後に呼び出すこと。
    InTable(const string &read_file_name_LBW) noexcept 
    : m_buffer(0), m_num_keep(0){
        //引数整合性チェック
        //ファイルが必要か否か
        {
            if(read_file_name_LBW.empty() || read_file_name_LBW == "NULL"){
                cerr << "Input file LBW is always necessary" << endl;
                terminate();
            }
        }

        //読み込んだファイルが正しいか確かめるための長さ12のバイト列。
        //0要素は表のエントリ数、1要素は盤面の1辺のマス数、2要素は盤面フェンス数、3要素は先手手持ち、4要素は後手手持ち、以降ZDD1葉経路数。
        //LBWファイル読み込み
        {
            unsigned char header_LBW[12];
            size_t elem_num_check_LBW = 0ULL;
            m_ifs_LBW.open(read_file_name_LBW, ifstream::binary);
            if(!m_ifs_LBW){
                cerr << "Input file LBW do not exist" << endl;
                terminate();
            }
            else {
                //ヘッダー読み込み
                cout << "read header LBW: ";
                for(int i = 0; i < 12; i++){
                    m_ifs_LBW.read((char*)header_LBW + i, 1U);
                    cout << (int)header_LBW[i] << " ";
                }
                cout << endl;

                //ヘッダー情報照合
                if(header_LBW[0] != table_entry_size){
                    cerr << "Entry size error in LBW file" << endl;
                    terminate();
                }
                if(header_LBW[1] != (unsigned char)N || header_LBW[2] != (unsigned char)L || header_LBW[3] != (unsigned char)B || header_LBW[4] != (unsigned char)W){
                    cerr << "Invalid (game setting N/L/B/W) : (file setting N/L/B/W) = " << "(" << N << "/" << L << "/" << B << "/" << W << ")" << " : " << "(" << (int)header_LBW[1] << "/" << (int)header_LBW[2] << "/" << (int)header_LBW[3] << "/" << (int)header_LBW[4] << ")" << endl;
                    terminate(); 
                }
                for(int i = 7; i > 0; i--){ //ファイルでは配置数を256進数で表現されているので元に戻す。
                    elem_num_check_LBW *= 256ULL;
                    elem_num_check_LBW += header_LBW[i+4];
                }
                if(elem_num_check_LBW != one_leaves_num_LBW) {
                    cerr << "Size Error" << endl;
                    terminate();
                }
            }
        }
    }
    ~InTable() noexcept {
        m_ifs_LBW.close();
    }
    unsigned int Order_Read_One_Elem() noexcept { //要素を1つ目のファイルから順に1つずつ取り出していく。引数の順番にファイルを読み込む。
        unsigned int val = 0;
        if(m_num_keep == 0) {//ファイルから読むのは1バイト、使いたいビット数を考慮。
            //ファイルからchar型で1文字ファイルから読み込む。
            if(m_ifs_LBW){
                m_ifs_LBW.read((char*)&m_buffer, 1U);
            }
            else{
                m_buffer = v_unknown;
            }
            m_num_keep = 8U/table_entry_size; //Charは8ビットなので1回でいくつ要素を取り出したか計算。
        }
        val = m_buffer & ((1ULL << table_entry_size) - 1); //表のサイズ分要素取り出し
        m_buffer =  m_buffer >> table_entry_size; //エントリサイズ分右にシフト。
        m_num_keep--; //1組値を使ったのでため込んだ分消費。
        return val;
    }     
};

//InTableを定義してバイナリファイルを読み込む。
//読み書きの制御。
class Table{
private:
    uint64_t *m_table; //ゲーム値書き込み表

public:
    Table(const string &read_file_name_LBW) noexcept 
    : m_table(new uint64_t [table_size64]){

        InTable in_table(read_file_name_LBW); //長い数字は1葉配置数に変える。
        //メモリにゲーム値表を作成
        //LBW、LWBどっちの配置の勝敗か分かるようにする
        //メモリにゲーム値表を作成。到達可能配置更新する時に必要な分だけ使う。
        //解析の際LもL+1もどちらの配置を同時に数えてしまうと盤上フェンス毎の内訳を考えにくくなるので分ける。
        unsigned long long int nunknown_LBW = 0ULL;
        unsigned long long int nblack_LBW = 0ULL;
        unsigned long long int nwhite_LBW = 0ULL;
        unsigned long long int nboth_LBW = 0ULL;

        for (unsigned long long int i = 0; i < one_leaves_num_LBW; i++) {
            unsigned int v = in_table.Order_Read_One_Elem();
            //盤面フェンス数Lの配置に対してのみ、勝敗結果の数を記録。
             if(v == v_unknown) {
                nunknown_LBW++;
            }
            else if(v == v_black) {
                lreachable = i;
                nblack_LBW++;
                black_reachable_num++;
                white_reachable_num++;
            }
            else if(v == v_white) {
                lreachable = i;
                nwhite_LBW++;
            } 
            else if(v == v_both){
                lreachable = i;
                nboth_LBW++;
                black_reachable_num++;
                white_reachable_num++;
            }
            set_only(i, v);
        }
        
        cout << "nblack_LBW  =  " << nblack_LBW << endl;
        cout << "nwhite_LBW  =  " << nwhite_LBW << endl;
        cout << "nboth_LBW  =  " << nboth_LBW << endl;
        cout << "nunknown_LBW  =  " << nunknown_LBW << endl;
        cout << "last reachable  =  " << lreachable << endl;
    }
    ~Table() noexcept {
        delete [] m_table;
    } //メモリ解法

    //指定したエントリサイズの表から値を取り出す。表はULL配列なのでエントリサイズに対応させる。
    unsigned int get(unsigned long long int table_id) const noexcept {
        if(table_id >= one_leaves_num_LBW){
            cerr << table_id << endl;
            terminate(); 
        }
        unsigned long long int id64 = table_id / (64ULL/table_entry_size); //ULL配列の何番目の要素にアクセスするか
        unsigned long long int id1 = (table_id % (64ULL/table_entry_size)) * table_entry_size; //アクセスした要素の何ビット目から読み込むか
        unsigned int v = ((1ULL << table_entry_size) - 1) & (unsigned int)(m_table[id64] >> (64ULL-id1-table_entry_size)); //取り出したい部分を下位エントリサイズビットに入るよう左シフトしマスクして取り出す。
        return v;
    }

    //指定したエントリサイズの表に値を入れる。表はULL配列なのでエントリサイズに対応させる。
    //最初に表を作るときに使う。更新フラグは立てない。
    void set_only(unsigned long long int table_id, unsigned int write_element) noexcept {
        unsigned long long int id64 = table_id / (64ULL/table_entry_size); //ULL配列の何番目の要素にアクセスするか
        unsigned long long int id1 = (table_id % (64ULL/table_entry_size)) * table_entry_size; //アクセスした要素の何ビット目から書き込むか
        unsigned long long int mask = ((1ULL << table_entry_size)- 1) << (64ULL-id1-table_entry_size); //書き込みたい部分にマスクを持っていく
        unsigned long long int t = m_table[id64] & ~mask; //マスクをビット反転して書き込みたい要素の部分を全て0にしておく
        m_table[id64] = t | ((unsigned long long int)write_element << (64ULL-id1-table_entry_size)); //要素を指定領域までシフトして入れる。
    }
};

//並列化で使う。スレッドでやり取り。
//bossとworkerはこのworkを通して到達可能な場合の手数を返す。
class Work {
private:
    unsigned long long int m_id; //担当している配置のID
    unsigned int m_parent; //親配置の到達可能状態
    int m_black_parent_num_child; //親が先手到達可能な時の子配置数
    int m_white_parent_num_child; //親が後手到達可能な時の子配置数

public:
    Work() noexcept {}
    Work(unsigned long long int id) noexcept : m_id(id) {} //IDを指定して設定するコンストラクタ

    //パラメータを設定する関数群
    void set_id(unsigned long long int id) noexcept { m_id = id;}
    void set_parent(unsigned int parent) noexcept { m_parent = parent; }
    void set_black_parent_num_child(int num_child) noexcept { m_black_parent_num_child = num_child; }
    void set_white_parent_num_child(int num_child) noexcept { m_white_parent_num_child = num_child; }

    //パラメータを返す関数群
    //配列は引数にパラメータを代入する形で返す
    unsigned long long int get_id() const noexcept { return m_id; }
    unsigned long long int get_parent() const noexcept { return m_parent; }
    int get_black_parent_num_child() const noexcept { return m_black_parent_num_child; }
    int get_white_parent_num_child() const noexcept { return m_white_parent_num_child; }
};

deque<Work *> deq_input;
deque<Work *> deq_output;
static void boss(const string &read_file_name_LBW) noexcept {
    Table table(read_file_name_LBW);
    
    unsigned long long int count_input = 0ULL;
    unsigned long long int count_output = 0ULL;
    //unsigned long long int test = 0;
    int nstack_work_idle = deq_input_size + deq_output_size + nworker;
    Work* stack_work_idle[nstack_work_idle];
    for(int i = 0; i < nstack_work_idle; i++) stack_work_idle[i] = new Work;
    
    while (true) {// 仕事がなくなるまで繰り返す
        unique_lock<mutex> lck(mtx); // ロック
        cv_boss.wait(lck, [&](){
            return (((deq_input.size() < deq_input_size) && (deq_output.size() < deq_output_size) && (count_input <= lreachable))
                    || (0 < deq_output.size())); });
                                        // deq_inputやdeq_outputが十分減るまで待つ(真だったら寝ないし、ロックも解放しない)
        // ロックの解放と寝るのはアトミック同時
        // 起きたなら、ロックは獲得している。起きてから式の評価を1回行い、偽ならばロックを解放して再び寝る。真ならば、コールバック。
        if ((deq_input.size() < deq_input_size) && (deq_output.size() < deq_output_size) && count_input <= lreachable) {
            // 後退解析の表を使う。
            // add only unknown id
            //今回は先手が到達可能配置を扱いたい。
            while(table.get(count_input) == v_unknown) {
                count_input++; 
                count_output++;
                if(count_input > lreachable){break;}
            }
            //count_inputがlunknownを越えていたら
            //  count_outputも越えていたらbreak、さもなくばcontinue。
            if(count_input > lreachable){
                if(count_output > lreachable){break;}
                else{continue;}
            }
            assert(table.get(count_input) != v_unknown);
            assert(nstack_work_idle >= 1);
            Work *pw = stack_work_idle[ --nstack_work_idle ];
            pw->set_id(count_input);
            pw->set_parent(table.get(count_input));
            deq_input.push_front(pw); //新たに仕事を追加する
            lck.unlock();
            count_input++;
            cv_worker.notify_one(); //他スレッドを起こす
        } else {
            if (0 < deq_output.size()) {
                assert(0 < deq_output.size());
                deque<Work *> deq_tmp; // use swap
                swap(deq_tmp, deq_output);
                // empty deq_output
                deq_output.clear();
                lck.unlock();
                count_output += deq_tmp.size();

                for (unsigned int workid = 0; workid < deq_tmp.size(); workid++) {
                    unsigned int parent_value = deq_tmp[workid]->get_parent(); //workerが子配置を列挙した時点の親の値。table.get(id)だと途中で値が変わり、未設定の子配置配列を読んでしまう。
                    if(parent_value == v_unknown){continue;} // worker担当配置は不明なら何もしない。

                    int black_actions = deq_tmp[workid]->get_black_parent_num_child();
                    int white_actions = deq_tmp[workid]->get_white_parent_num_child();

                    if(max_black_actions < black_actions){max_black_actions = black_actions;}
                    if(max_white_actions < white_actions){max_white_actions = white_actions;}

                    total_black_actions += black_actions;
                    total_white_actions += white_actions;
                }
                for(size_t i = 0; i < deq_tmp.size(); i++){
                    assert(nstack_work_idle < 2048);
                    stack_work_idle[ nstack_work_idle++ ] = deq_tmp[i];
                }
            }
            if (lreachable+1 <= count_output) break;
        }
    }

    cout << "max black actions = " << max_black_actions << endl;
    cout << "total black actions = " << total_black_actions << endl;
    cout << "black reachable num = " << black_reachable_num << endl;
    cout << "ave black actions = " << (black_reachable_num > 0 ? (double)total_black_actions / black_reachable_num : 0) << endl;
    
    cout << "max white actions = " << max_white_actions << endl;
    cout << "total white actions = " << total_white_actions << endl;
    cout << "white reachable num = " << white_reachable_num << endl;
    cout << "ave white actions = " << (white_reachable_num > 0 ? (double)total_white_actions / white_reachable_num : 0) << endl;
    
    cout << "max actions = " << max(max_black_actions, max_white_actions) << endl;
    cout << "total actions = " << total_black_actions + total_white_actions << endl;
    cout << "reachable num = " << black_reachable_num + white_reachable_num << endl;
    cout << "ave actions = " << ((black_reachable_num + white_reachable_num) > 0 ? (double)(total_black_actions + total_white_actions) / (black_reachable_num + white_reachable_num) : 0) << endl;

    unique_lock<mutex> lck(mtx); //ロック
    flag_worker_quit = true; //仕事が終わったことを表すフラグ
    cv_worker.notify_all(); //仕事が全部終了したのでworkerを全て起こす。起きたときに仕事終了フラグを確認して止まる。
}

//与えられたIDの配置の子配置のIDを全て列挙する役割。
static void worker() noexcept {
    Work *w;
    while (true) { //仕事を全て終えるまで繰り返す
        unique_lock<mutex> lck(mtx); //ロック
        //ロック解除して寝る
        cv_worker.wait(lck, [&](){ if (0 < deq_input.size()) return true;
                                    return flag_worker_quit; }); //仕事がある or (表に記入すべきものがあり、表がkeepされていない) or 仕事が全て終わっている になるまで待つ
                                    //ロック
        if (flag_worker_quit) break;//仕事が全て完了したなら終わり
        assert(0 < deq_input.size()); //仕事があるなら
        w = deq_input.back();
        deq_input.pop_back();
        lck.unlock();//ロック解除
        
        Posi posi(w->get_id(), zdd); //Posiが参照する配置を登録。
        unsigned int parent = w->get_parent();

        //親が先手到達可能ならそのまま先手の可能手を計算して子配置を渡す。
        if((parent & 1) != 0){ //1bit目が1なら親配置は先手到達可能
            int nchild = posi.compute_naction(B>0); // posiの合法手列挙。actionsに可能手をすべて記録し可能手数を返す。
            w->set_black_parent_num_child(nchild);
        }
        else{    
            w->set_black_parent_num_child(0);
        }
        //親が後手到達可能なら盤面を反転。これで後手視点に。そこから子配置を作り再び反転。それが先手到達可能配置。
        if((parent & 2) != 0){ //2bit目が1なら親配置は後手到達可能
            unsigned long long int flip_id = posi.compute_id_flip(zdd);
            Posi flip_posi(flip_id, zdd);
            int nchild = flip_posi.compute_naction(W>0);
            w->set_white_parent_num_child(nchild);
        }
        else{
            w->set_white_parent_num_child(0);
        }
        
        lck.lock();//ロック
        deq_output.push_front(w);
        lck.unlock();//ロック解除
        cv_boss.notify_one();//起きれるやつがいたら起こしてからコールバック
    }
}

int main(int argc, char *argv[]) {
    L = atoi(argv[2]);
    B = atoi(argv[3]);
    W = atoi(argv[4]);
    assert(atoi(argv[1]) == N && atoi(argv[2]) == L && atoi(argv[3]) == B && atoi(argv[4]) == W);

    //最初にZDDを構築。
    zdd = ZDD(L);
    one_leaves_num_LBW = zdd.One_Leaves_Num_L();

    table_size64 = (one_leaves_num_LBW + 64ULL/table_entry_size - 1) / (64ULL/table_entry_size);
    lreachable = one_leaves_num_LBW-1ULL; //今回解析の親配置となるのは盤面フェンス数Lの配置に対してだけ。
    //zdd.View_Root_Info();

    thread th_boss([=]{boss(argv[5]);});
    thread th_worker[nworker];
    for(int workerid = 0; workerid < nworker; workerid++){
        th_worker[workerid] = thread([&]{worker();});
    }

    th_boss.join();
    for(int workerid = 0; workerid < nworker; workerid++){
        th_worker[workerid].join();
    }

    return 0;
}