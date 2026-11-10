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
unsigned int table_gamevalue_entry_size = 8U;
unsigned int table_reachable_entry_size = 2U;
unsigned long long int one_leaves_num_LBW = 0ULL; //ZDDのうち合法手数解析対象にするのはL側だけ。読み込むのもLBWファイルのみ

unsigned long long int gamevalue_table_size64 = 0ULL; //uint64を8ビットエントリにした時のゲーム値表のサイズ。
unsigned long long int reachable_table_size64 = 0ULL; //uint64を2ビットエントリにした時のゲーム値表のサイズ。
constexpr int nworker = 1; //分割数（特に指定はない。後で調整。）
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

unsigned long long int n_zz_dtow = 0ULL, n_zz_ltod = 0ULL, n_zz_ltow = 0ULL, lreachable = 0ULL; //ツークツワンクの数と到達可能配置の最大ID
// unsigned long long int nwin = 0ULL, nlose = 0ULL, nunknown = 0ULL;
bool flag_worker_quit = false; //仕事が終わったことを表すフラグ

enum { v_unknown = 0, v_black = 1, v_white = 2, v_both = 3}; //到達可能の値

//ファイル命名規則quoridor_reachable_NLBW（盤面サイズ、盤上フェンス、先手手持ち、後手手持ち）
//LBWのみ読み込み
class InTable{
private:
    unsigned char m_buffer;
    unsigned int m_num_keep;
    unsigned int table_entry_size;
    ifstream m_ifs_LBW;
public:
    InTable() = delete; //引数なしコンストラクタを禁止する。
    //呼び出すコンストラクタは必ずこっち。ZDD構築後に呼び出すこと。
    InTable(int b, int w, unsigned int entry_size, const string &read_file_name_LBW) noexcept 
    : m_buffer(0), m_num_keep(0), table_entry_size(entry_size){
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
                if(header_LBW[1] != (unsigned char)N || header_LBW[2] != (unsigned char)L || header_LBW[3] != (unsigned char)b || header_LBW[4] != (unsigned char)w){
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

//InTableを定義して到達可能配置のバイナリファイルを読み込む。
//読み書きの制御。
class Reachability_Table{
private:
    unsigned int table_entry_size = table_reachable_entry_size;
    uint64_t *m_table; //ゲーム値書き込み表

public:
    Reachability_Table(const string &read_file_name_LBW) noexcept 
    : m_table(new uint64_t [reachable_table_size64]()){

        InTable in_table(B, W, table_entry_size, read_file_name_LBW); //長い数字は1葉配置数に変える。
        //メモリにゲーム値表を作成
        //LBW、LWBどっちの配置の勝敗か分かるようにする
        //メモリにゲーム値表を作成。到達可能配置更新する時に必要な分だけ使う。
        //解析の際LもL+1もどちらの配置を同時に数えてしまうと盤上フェンス毎の内訳を考えにくくなるので分ける。

        for (unsigned long long int i = 0; i < one_leaves_num_LBW; i++) {
            unsigned int v = in_table.Order_Read_One_Elem();
            //盤面フェンス数Lの配置に対してのみ、勝敗結果の数を記録。
            if(v == v_black || v == v_both) {
                lreachable = i;
            }
            set_only(i, v);
        }
    }
    ~Reachability_Table() noexcept {
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

//InTableを定義して後退解析のバイナリファイルを読み込む。
//読み書きの制御。
class GameValue_Table{
private:
    unsigned int table_entry_size = table_gamevalue_entry_size;
    uint64_t *m_table; //ゲーム値書き込み表

public:
    GameValue_Table(int b, int w, const string &read_file_name_LBW) noexcept 
    : m_table(new uint64_t [gamevalue_table_size64]()){

        InTable in_table(b, w, table_entry_size, read_file_name_LBW); //長い数字は1葉配置数に変える。
        //メモリにゲーム値表を作成
        //LBW、LWBどっちの配置の勝敗か分かるようにする
        //メモリにゲーム値表を作成。到達可能配置更新する時に必要な分だけ使う。
        //解析の際LもL+1もどちらの配置を同時に数えてしまうと盤上フェンス毎の内訳を考えにくくなるので分ける。

        //nwin = 0;
        //nlose = 0;
        //nunknown = 0;
        for (unsigned long long int i = 0; i < one_leaves_num_LBW; i++) {
            unsigned int v = in_table.Order_Read_One_Elem();
            set_only(i, v);
            //if(v==(1U << table_gamevalue_entry_size)-1){nunknown++;}
            //else if(v%2==0){nlose++;}
            //else if(v%2==1){nwin++;}
        }
    }
    ~GameValue_Table() noexcept {
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
    unsigned int m_reachability; //親配置の到達可能状態
    unsigned int m_gamevalue; //参照配置の自分のゲーム値
    unsigned int m_skip_white_gamevalue; //参照配置において自分が手番を飛ばした時の相手のゲーム値

public:
    Work() noexcept {}
    Work(unsigned long long int id) noexcept : m_id(id) {} //IDを指定して設定するコンストラクタ

    //パラメータを設定する関数群
    void set_id(unsigned long long int id) noexcept { m_id = id;}
    void set_reachability(unsigned int reachability) noexcept { m_reachability = reachability; }
    void set_gamevalue(unsigned int value) noexcept { m_gamevalue = value; }
    void set_skip_white_gamevalue(unsigned int value) noexcept { m_skip_white_gamevalue = value; }

    //パラメータを返す関数群
    //配列は引数にパラメータを代入する形で返す
    unsigned long long int get_id() const noexcept { return m_id; }
    unsigned long long int get_reachability() const noexcept { return m_reachability; }
    unsigned long long int get_gamevalue() const noexcept { return m_gamevalue; }
    unsigned long long int get_skip_white_gamevalue() const noexcept { return m_skip_white_gamevalue; }
};

deque<Work *> deq_input;
deque<Work *> deq_output;
Reachability_Table *p_reachability_table;
GameValue_Table *p_gamevalue_table;
GameValue_Table *p_flip_gamevalue_table;
static void boss(const string &reachability_read_file_name_LBW, const string &gamevalue_read_file_name_LBW, const string &gamevalue_read_file_name_LWB) noexcept {
    Reachability_Table reachability_table(reachability_read_file_name_LBW);
    GameValue_Table gamevalue_table(B, W, gamevalue_read_file_name_LBW);
    GameValue_Table flip_gamevalue_table(W, B, gamevalue_read_file_name_LWB);
    p_reachability_table = &reachability_table;
    p_gamevalue_table = &gamevalue_table;
    p_flip_gamevalue_table = &flip_gamevalue_table;
    
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
            while(reachability_table.get(count_input) == v_unknown || reachability_table.get(count_input) == v_white) {
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
            assert(reachability_table.get(count_input) != v_unknown && reachability_table.get(count_input) != v_white);
            assert(nstack_work_idle >= 1);
            Work *pw = stack_work_idle[ --nstack_work_idle ];
            pw->set_id(count_input);
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
                    unsigned int parent_value = deq_tmp[workid]->get_reachability(); //workerが子配置を列挙した時点の親の値。table.get(id)だと途中で値が変わり、未設定の子配置配列を読んでしまう。
                    if(parent_value == v_unknown || parent_value == v_white){continue;} // 先手到達可能でないなら何もしない。

                    unsigned int gamevalue = deq_tmp[workid]->get_gamevalue();
                    unsigned int skip_white_gamevalue = deq_tmp[workid]->get_skip_white_gamevalue();
                    
                    if(gamevalue == (1U << table_gamevalue_entry_size)-1){
                        if(skip_white_gamevalue % 2 == 0){
                            n_zz_dtow++;
                        }
                    }
                    else if(gamevalue % 2 == 0){
                        if(skip_white_gamevalue == (1U << table_gamevalue_entry_size)-1){
                            n_zz_ltod++;
                        }
                        else if(skip_white_gamevalue % 2 == 0){
                            n_zz_ltow++;
                        }
                    }
                }
                for(size_t i = 0; i < deq_tmp.size(); i++){
                    assert(nstack_work_idle < 2048);
                    stack_work_idle[ nstack_work_idle++ ] = deq_tmp[i];
                }
            }
            if (lreachable+1 <= count_output) break;
        }
    }

    cout << "zugzwang draw to win = " << n_zz_dtow << endl;
    cout << "zugzwang lose to draw = " << n_zz_ltod << endl;
    cout << "zugzwang lose to win = " << n_zz_ltow << endl;
    //cout << one_leaves_num_LBW << " " << nwin << " " << nlose << " " << nunknown << endl;

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
        
        unsigned long long int table_id = w->get_id();
        Posi posi(table_id, zdd); //workに与えられた配置のIDから配置を作る
        //cout << "compare" << endl;
        //posi.View_Position();
        unsigned long long int flip_table_id = posi.compute_id_flip(zdd);
        //posi.make_posi(flip_table_id, zdd);
        //posi.View_Position();

        w->set_reachability(p_reachability_table->get(table_id));
        w->set_gamevalue(p_gamevalue_table->get(table_id));
        w->set_skip_white_gamevalue(p_flip_gamevalue_table->get(flip_table_id));

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

    reachable_table_size64 = (one_leaves_num_LBW + 64ULL/table_reachable_entry_size - 1) / (64ULL/table_reachable_entry_size);
    gamevalue_table_size64 = (one_leaves_num_LBW + 64ULL/table_gamevalue_entry_size - 1) / (64ULL/table_gamevalue_entry_size);
    lreachable = one_leaves_num_LBW-1ULL; //今回解析の親配置となるのは盤面フェンス数Lの配置に対してだけ。
    //zdd.View_Root_Info();

    thread th_boss([=]{boss(argv[5], argv[6], argv[7]);});
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