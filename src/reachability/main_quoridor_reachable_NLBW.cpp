// 到達可能配置は先手視点で見るのではなく、先手後手各々の視点で見る。
// そのため、後退解析と異なり視点反転を行わない。
// 今回は手持ちの組合せを考慮。
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
unsigned long long int one_leaves_num = 0ULL; //ZDD全体の1葉配置数をここに入れる。
unsigned long long int one_leaves_num_L = 0ULL; //ZDDのうち盤面フェンス数Lの1葉配置数をここに入れる。
unsigned long long int one_leaves_num_Lp1 = 0ULL; //ZDDのうち盤面フェンス数L+1の1葉配置数をここに入れる。

// 先手後手手持ち枚数が異なる場合、その手持ちを反転させた表を見る必要がある。
// 後退解析では子配置は先手行動後の反転配置
unsigned long long int one_leaves_num_LBW = 0ULL; //解析対象。これを基準にしてファイルを指定。
unsigned long long int one_leaves_num_Lp1Bm1W = 0ULL; //先手がファイルを使った時に参照する子配置
unsigned long long int one_leaves_num_Lp1BWm1 = 0ULL; //後手がフェンスを使った時に参照する子配置


unsigned long long int table_size64 = 0ULL; //uint64を2ビットエントリにした時のゲーム値表のサイズ。
constexpr int nworker = 2; //分割数（特に指定はない。後で調整。）
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

unsigned long long int nblack = 0ULL, nwhite = 0ULL, nboth = 0ULL, nunknown = 0ULL, lreachable; //先手到達可能配置数、後手到達可能配置数、両手到達可能配置数、不明配置数、いずれかが到達可能配置の最大ID
bool flag_worker_quit = false; //仕事が終わったことを表すフラグ

enum { v_unknown = 0, v_black = 1, v_white = 2, v_both = 3}; //表に登録する値

//ファイル命名規則quoridor_reachable_NLBW（盤面サイズ、盤上フェンス、先手手持ち、後手手持ち）
//解析対象ファイルはNLBWとNLWBの2つのファイルを入力として扱う（BWの手持ち反転したものがWB）
//解析の際に参照するためにN(L+1)(B-1)WとN(L+1)B(W-1)の2つが追加で必要。
class InTable_By_ThreeFile{
private:
    unsigned char m_buffer;
    unsigned int m_num_keep;
    unsigned long long int m_total_read_num = 0; //これまで読み込んだ要素数が一つ目のファイルの表のサイズを越えたら二つ目のファイルを読み込む
    ifstream m_ifs_LBW;
    ifstream m_ifs_Lp1Bm1W;
    ifstream m_ifs_Lp1BWm1;
public:
    InTable_By_ThreeFile() = delete; //引数なしコンストラクタを禁止する。
    //呼び出すコンストラクタは必ずこっち。ZDD構築後に呼び出すこと。
    InTable_By_ThreeFile(const string &read_file_name_LBW, const string &read_file_name_Lp1Bm1W, const string &read_file_name_Lp1BWm1) noexcept 
    : m_buffer(0), m_num_keep(0){
        //引数整合性チェック
        //ファイルが必要か否か
        {
            if(read_file_name_LBW.empty() || read_file_name_LBW == "NULL"){
                cerr << "Input file LBW is always necessary" << endl;
                terminate();
            }
            if(B > 0 && (read_file_name_Lp1Bm1W.empty() || read_file_name_Lp1Bm1W == "NULL")){
                cerr << "Input file Lp1WBm1 is necessary when B > 0" << endl;
                terminate();
            }
            if(W > 0  && (read_file_name_Lp1BWm1.empty() || read_file_name_Lp1BWm1 == "NULL")){
                cerr << "Input file Lp1BWm1 is necessary when W > 0 " << endl;
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
        //Lp1WBm1ファイル読み込み
        if(B > 0){
            unsigned char header_Lp1Bm1W[12];
            size_t elem_num_check_Lp1Bm1W = 0ULL;
            cout << "read_file_name_Lp1Bm1W" << endl;
            m_ifs_Lp1Bm1W.open(read_file_name_Lp1Bm1W, ifstream::binary);
            if(!m_ifs_Lp1Bm1W){
                cerr << "Input file Lp1WBm1 do not exist" << endl;
                terminate();
            }
            else {       
                for(int i = 0; i < 12; i++){
                    m_ifs_Lp1Bm1W.read((char*)header_Lp1Bm1W + i, 1U);
                    cout << (int)header_Lp1Bm1W[i] << " ";
                }
                cout << endl;

                //ヘッダー情報照合
                if(header_Lp1Bm1W[0] != table_entry_size){
                    cerr << "Entry size error in Lp1WBm1 file" << endl;
                    terminate();
                }
                if(header_Lp1Bm1W[1] != (unsigned char)N || header_Lp1Bm1W[2] != (unsigned char)(L+1) || header_Lp1Bm1W[3] != (unsigned char)(B-1) || header_Lp1Bm1W[4] != (unsigned char)W){
                    cerr << "Invalid (game setting N/(L+1)/(B-1)/W) : (file setting N/(L+1)/(B-1)/W) = " << "(" << N << "/" << L+1 << "/" << B-1 << "/" << W << ")" << " : " << "(" << (int)header_Lp1Bm1W[1] << "/" << (int)header_Lp1Bm1W[2] << "/" << (int)header_Lp1Bm1W[3] << "/" << (int)header_Lp1Bm1W[4] << ")" << endl;
                    terminate(); 
                }
                for(int i = 7; i > 0; i--){ //ファイルでは配置数を256進数で表現されているので元に戻す。
                    elem_num_check_Lp1Bm1W *= 256ULL;
                    elem_num_check_Lp1Bm1W += header_Lp1Bm1W[i+4];
                }
                if(elem_num_check_Lp1Bm1W != one_leaves_num_Lp1Bm1W) {
                    cerr << "Size Error" << endl;
                    terminate();
                }
            }
        }
        //Lp1BWm1ファイル読み込み
        if(W > 0){
            unsigned char header_Lp1BWm1[12];
            size_t elem_num_check_Lp1BWm1 = 0ULL;
            cout << "read_file_name_Lp1BWm1" << endl;
            m_ifs_Lp1BWm1.open(read_file_name_Lp1BWm1, ifstream::binary);
            if(!m_ifs_Lp1BWm1){
                cerr << "Input file Lp1BWm1 do not exist" << endl;
                terminate();
            }
            else {       
                for(int i = 0; i < 12; i++){
                    m_ifs_Lp1BWm1.read((char*)header_Lp1BWm1 + i, 1U);
                    cout << (int)header_Lp1BWm1[i] << " ";
                }
                cout << endl;

                //ヘッダー情報照合
                if(header_Lp1BWm1[0] != table_entry_size){
                    cerr << "Entry size error in Lp1BWm1 file" << endl;
                    terminate();
                }
                if(header_Lp1BWm1[1] != (unsigned char)N || header_Lp1BWm1[2] != (unsigned char)(L+1) || header_Lp1BWm1[3] != (unsigned char)B || header_Lp1BWm1[4] != (unsigned char)(W-1)){
                    cerr << "Invalid (game setting N/(L+1)/B/(W-1)) : (file setting N/(L+1)/B)/(W-1)) = " << "(" << N << "/" << L+1 << "/" << B << "/" << W-1 << ")" << " : " << "(" << (int)header_Lp1BWm1[1] << "/" << (int)header_Lp1BWm1[2] << "/" << (int)header_Lp1BWm1[3] << "/" << (int)header_Lp1BWm1[4] << ")" << endl;
                    terminate(); 
                }
                for(int i = 7; i > 0; i--){ //ファイルでは配置数を256進数で表現されているので元に戻す。
                    elem_num_check_Lp1BWm1 *= 256ULL;
                    elem_num_check_Lp1BWm1 += header_Lp1BWm1[i+4];
                }
                if(elem_num_check_Lp1BWm1 != one_leaves_num_Lp1BWm1) {
                    cerr << "Size Error" << endl;
                    terminate();
                }
            }
        }
    }
    ~InTable_By_ThreeFile() noexcept {
        m_ifs_LBW.close();
        m_ifs_Lp1Bm1W.close();
        m_ifs_Lp1BWm1.close();
    }
    unsigned int Order_Read_One_Elem() noexcept { //要素を1つ目のファイルから順に1つずつ取り出していく。引数の順番にファイルを読み込む。
        //読み込みファイルが切り替わった時を検知。1エントリ8ビットの現在では無意味。
        if (m_total_read_num == one_leaves_num_LBW || 
            m_total_read_num == one_leaves_num_LBW + one_leaves_num_Lp1Bm1W) {
            m_num_keep = 0; 
            m_buffer = 0;
        }
        unsigned int val = 0;
        if(m_num_keep == 0) {//ファイルから読むのは1バイト、使いたいビット数を考慮。
            //ファイルからchar型で1文字ファイルから読み込む。
            if(m_total_read_num < one_leaves_num_LBW){
                if(m_ifs_LBW){
                    m_ifs_LBW.read((char*)&m_buffer, 1U);
                }
                else{
                    m_buffer = v_unknown;
                }
            }
            else if(m_total_read_num < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W){
                if(m_ifs_Lp1Bm1W){
                    m_ifs_Lp1Bm1W.read((char*)&m_buffer, 1U);
                }
                else{
                    m_buffer = v_unknown;
                }
            }
            else{
                if(m_ifs_Lp1BWm1){
                    m_ifs_Lp1BWm1.read((char*)&m_buffer, 1U);
                }
                else{
                    m_buffer = v_unknown;
                }
            }
            m_num_keep = 8U/table_entry_size; //Charは8ビットなので1回でいくつ要素を取り出したか計算。
        }
        val = m_buffer & ((1ULL << table_entry_size) - 1); //表のサイズ分要素取り出し
        m_buffer =  m_buffer >> table_entry_size; //エントリサイズ分右にシフト。
        m_num_keep--; //1組値を使ったのでため込んだ分消費。
        m_total_read_num++; //読み込んだエントリ数を増やす。
        //cout << val << endl;
        return val;
    }     
};

//InTable_By_ThreeFileを定義してバイナリファイルを読み込む。
//読み書きの制御。
class Table_By_ThreeFile{
private:
    uint64_t *m_table; //ゲーム値書き込み表

    uint64_t *m_changed_flags; //更新フラグ管理表（1エントリ = 1ビット）
    size_t m_flags_size64; //更新フラグ管理表サイズ

public:
    Table_By_ThreeFile(const string &read_file_name_LBW, const string &read_file_name_Lp1Bm1W, const string &read_file_name_Lp1BWm1) noexcept 
    : m_table(new uint64_t [table_size64]), m_changed_flags(nullptr){

        m_flags_size64 = (one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1 + 64ULL - 1ULL) / 64ULL;
        m_changed_flags = new uint64_t[m_flags_size64];
        //更新フラグ初期化。全て更新されていない状態にする。
        for(size_t i = 0; i < m_flags_size64; i++){
            m_changed_flags[i] = 0ULL;
        }

        cout << "read file name LBW = " << read_file_name_LBW << endl;
        cout << "read file name Lp1Bm1W = " << read_file_name_Lp1Bm1W << endl;
        cout << "read file name Lp1BWm1 = " << read_file_name_Lp1BWm1 << endl;

        InTable_By_ThreeFile in_table_by_threefile(read_file_name_LBW, read_file_name_Lp1Bm1W, read_file_name_Lp1BWm1); //長い数字は1葉配置数に変える。
        //メモリにゲーム値表を作成
        //LBW、LWBどっちの配置の勝敗か分かるようにする
        //メモリにゲーム値表を作成。到達可能配置更新する時に必要な分だけ使う。
        //解析の際LもL+1もどちらの配置を同時に数えてしまうと盤上フェンス毎の内訳を考えにくくなるので分ける。
        unsigned long long int nunknown_LBW = 0ULL;
        unsigned long long int nblack_LBW = 0ULL;
        unsigned long long int nwhite_LBW = 0ULL;
        unsigned long long int nboth_LBW = 0ULL;
        unsigned long long int nunknown_Lp1Bm1W = 0ULL;
        unsigned long long int nblack_Lp1Bm1W = 0ULL;
        unsigned long long int nwhite_Lp1Bm1W = 0ULL;
        unsigned long long int nboth_Lp1Bm1W = 0ULL;
        unsigned long long int nunknown_Lp1BWm1 = 0ULL;
        unsigned long long int nblack_Lp1BWm1 = 0ULL;
        unsigned long long int nwhite_Lp1BWm1 = 0ULL;
        unsigned long long int nboth_Lp1BWm1 = 0ULL;
        for (unsigned long long int i = 0; i < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1; i++) {
            unsigned int v = in_table_by_threefile.Order_Read_One_Elem();
            //盤面フェンス数Lの配置に対してのみ、勝敗結果の数を記録。
             if(v == v_unknown) {
                nunknown++;
                if(i < one_leaves_num_LBW){nunknown_LBW++;}
                else if(i < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W) {nunknown_Lp1Bm1W++;}
                else{nunknown_Lp1BWm1++;}
            }
            else if(v == v_black) {
                if(i < one_leaves_num_LBW){
                    lreachable = i; //探索対象の親配置はLファイルに属する配置だけ。
                    nblack_LBW++;
                } 
                else if(i < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W) {nblack_Lp1Bm1W++;}
                else{nblack_Lp1BWm1++;}
            }
            else if(v == v_white) {
                if(i < one_leaves_num_LBW){
                    lreachable = i; //探索対象の親配置はLファイルに属する配置だけ。
                    nwhite_LBW++;
                } 
                else if(i < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W) {nwhite_Lp1Bm1W++;}
                else{nwhite_Lp1BWm1++;}
            } 
            else if(v == v_both){
                if(i < one_leaves_num_LBW){
                    lreachable = i; //探索対象の親配置はLファイルに属する配置だけ。
                    nboth_LBW++;
                } 
                else if(i < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W) {nboth_Lp1Bm1W++;}
                else{nboth_Lp1BWm1++;}
            }
            set_only(i, v);
        }
        
        cout << "table LBW" << endl;
        cout << "nblack_LBW  =  " << nblack_LBW << endl;
        cout << "nwhite_LBW  =  " << nwhite_LBW << endl;
        cout << "nboth_LBW  =  " << nboth_LBW << endl;
        cout << "nunknown_LBW  =  " << nunknown_LBW << endl;
        cout << endl;
        cout << "table Lp1Bm1W" << endl;
        cout << "nblack_Lp1Bm1W  =  " << nblack_Lp1Bm1W << endl;
        cout << "nwhite_Lp1Bm1W  =  " << nwhite_Lp1Bm1W << endl;
        cout << "nboth_Lp1Bm1W  =  " << nboth_Lp1Bm1W << endl;
        cout << "nunknown_Lp1Bm1W  =  " << nunknown_Lp1Bm1W << endl;
        cout << endl;
        cout << "table Lp1BWm1" << endl;
        cout << "nblack_Lp1BWm1  =  " << nblack_Lp1BWm1 << endl;
        cout << "nwhite_Lp1BWm1  =  " << nwhite_Lp1BWm1 << endl;
        cout << "nboth_Lp1BWm1  =  " << nboth_Lp1BWm1 << endl;
        cout << "nunknown_Lp1BWm1  =  " << nunknown_Lp1BWm1 << endl;
        cout << endl;
        cout << "nunknown  =  " << nunknown << endl;
        cout << "last reachable  =  " << lreachable << endl;

        //新しく判明した勝敗を実行後出力したいのでここでリセット。
        nblack = 0; 
        nwhite = 0; 
        nboth = 0; 
    }
    ~Table_By_ThreeFile() noexcept {
        delete [] m_table;
        delete [] m_changed_flags;
    } //メモリ解法

    //指定したエントリサイズの表から値を取り出す。表はULL配列なのでエントリサイズに対応させる。
    unsigned int get(unsigned long long int table_id) const noexcept {
        if(table_id >= one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1){
            cerr << table_id << endl;
            terminate(); 
        }
        unsigned long long int id64 = table_id / (64ULL/table_entry_size); //ULL配列の何番目の要素にアクセスするか
        unsigned long long int id1 = (table_id % (64ULL/table_entry_size)) * table_entry_size; //アクセスした要素の何ビット目から読み込むか
        unsigned int v = ((1ULL << table_entry_size) - 1) & (unsigned int)(m_table[id64] >> (64ULL-id1-table_entry_size)); //取り出したい部分を下位エントリサイズビットに入るよう左シフトしマスクして取り出す。
        return v;
    }
    
    //指定したエントリサイズの表に値を入れる。表はULL配列なのでエントリサイズに対応させる。
    //後退解析中に値が書き換わったら、対応するビットに1を立てる。
    //更新するのはLファイルのみの制限をかける。
    void set_update(unsigned long long int table_id, unsigned int write_element) noexcept {
        if(table_id >= one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1){
            cerr << " cannot set update. ID mismatch." << endl;
            terminate(); 
        }
        set_only(table_id, write_element);

        //値がセットされたら、フラグ配列の該当ビットを 1 (更新) に書き換える。
        unsigned long long int flag_id64 = table_id / 64ULL;
        unsigned long long int flag_bit_pos = table_id % 64ULL;
        m_changed_flags[flag_id64] |= (1ULL << (63ULL - flag_bit_pos));
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

    //1エントリ1ビットの配列から更新フラグ(0か1)を抽出して判定する関数
    //ファイルに書き出す時、更新するか否かを返す
    bool is_changed(unsigned long long int table_id) const noexcept {
        if(table_id >= one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1){
            cerr << "cannot set update. ID mismatch." << endl;
            terminate(); 
        }
        unsigned long long int id64 = table_id / 64ULL;       // uint64_t配列のインデックス
        unsigned long long int bit_pos = table_id % 64ULL;   // uint64_t内のビット位置 (0〜63)
        
        // 指定したビットが 1 であれば true（更新済み）を返す
        return (m_changed_flags[id64] & (1ULL << (63ULL - bit_pos))) != 0ULL;
    }
};

//並列化で使う。スレッドでやり取り。m_array_idのサイズを増やせばいい。
//ある配置の可能手数と、可能手で遷移する子配置のID集合を持つ。
class Work {
private:
    unsigned long long int m_id; //担当している配置のID
    unsigned int m_parent; //親配置の到達可能状態
    int m_black_parent_num_child; //親が先手到達可能な時の子配置数
    int m_white_parent_num_child; //親が後手到達可能な時の子配置数
    unsigned long long int m_black_parent_array_id[3000]; //親が先手到達可能な時に使う後手到達可能配置の集合
    unsigned long long int m_white_parent_array_id[3000]; //親が後手到達可能な時に使う先手到達可能配置の集合

public:
    Work() noexcept {}
    Work(unsigned long long int id) noexcept : m_id(id) {}

    void set_id(unsigned long long int id) noexcept { m_id = id;}
    void set_parent(unsigned int parent) noexcept { m_parent = parent; }
    void set_black_parent_array_id(int num_child, unsigned long long int array_id[]) noexcept {
        if((m_parent & 1) == 0){return;} //親が先手じゃないなら使わない
        m_black_parent_num_child = num_child;
        if(num_child <= 0) return;
        for(int childid = 0; childid < m_black_parent_num_child; childid++) {
            m_black_parent_array_id[childid] = array_id[childid];
        }
    }
    void set_white_parent_array_id(int num_child, unsigned long long int array_id[]) noexcept {
        if((m_parent & 2) == 0){return;} //親が後手じゃないなら使わない
        m_white_parent_num_child = num_child;
        if(num_child <= 0) return;
        for(int childid = 0; childid < m_white_parent_num_child; childid++) {
            m_white_parent_array_id[childid] = array_id[childid];
        }
    }

    unsigned long long int get_id() const noexcept { return m_id; }
    unsigned long long int get_parent() const noexcept { return m_parent; }
    int get_black_parent_num_child() const noexcept { return m_black_parent_num_child; }
    void get_black_parent_array_id(int num_child, unsigned long long int array_id[]) const noexcept {
        if((m_parent & 1) == 0){return;} //親が先手じゃないなら使わない
        for(int childid = 0; childid < num_child; childid++) {
            array_id[childid] = m_black_parent_array_id[childid];
        }
    }
    int get_white_parent_num_child() const noexcept { return m_white_parent_num_child; }
    void get_white_parent_array_id(int num_child, unsigned long long int array_id[]) const noexcept {
        if((m_parent & 2) == 0){return;} //親が後手じゃないなら使わない
        for(int childid = 0; childid < num_child; childid++) {
            array_id[childid] = m_white_parent_array_id[childid];
        }
    }
};

class OutTable_By_ThreeFile {
private:
    string m_filename_LBW;
    string m_filename_Lp1Bm1W;
    string m_filename_Lp1BWm1;

public:
    OutTable_By_ThreeFile() = delete;
    
    //コンストラクタ自身ではファイルの有無の判定やヘッダーの自動生成は行わない。
    //後退解析実行前にファイルが存在しないならヘッダーと不明で埋められたファイルを作るものとする。
    OutTable_By_ThreeFile(const string &write_file_name_LBW, const string &write_file_name_Lp1Bm1W, const string &write_file_name_Lp1BWm1) noexcept {
        m_filename_LBW = write_file_name_LBW;
        m_filename_Lp1Bm1W = write_file_name_Lp1Bm1W;
        m_filename_Lp1BWm1 = write_file_name_Lp1BWm1;
    }

    ~OutTable_By_ThreeFile() noexcept {}

    //解析の前に呼び出す、土台ファイル作成用の静的関数
    //ファイルが存在しない（またはオープンできない）場合のみ、確実にヘッダーと不明データを書き込み
    static void Create_Initial_File(const string &write_file_name_LBW, const string &write_file_name_Lp1Bm1W, const string &write_file_name_Lp1BWm1) noexcept {
        //生成ファイル名定義
        {
            // LBW
            string filename_LBW = write_file_name_LBW;
            if (filename_LBW.empty() || filename_LBW == "NULL") {
                filename_LBW = "quoridor_reachable_N" + std::to_string(N) + "L" + std::to_string(L) + "B" + std::to_string(B) + "W" + std::to_string(W) + ".bin";
            }
            //まずファイルがすでに存在するかどうかチェックする（開ければ存在する）
            ifstream ifs(filename_LBW, ifstream::binary);
            if (ifs) {
                // ファイルが存在する場合は何もしない（既存データを保護する）
                cout << "Initial file checking: " << filename_LBW << " already exists. Skip initialization." << endl;
                ifs.close();
            }
            else{
                //ここからファイル新規作成処理
                ofstream ofs(filename_LBW, ofstream::binary);
                if (!ofs) {
                    cerr << "Error: Cannot create initial base file: " << filename_LBW << endl;
                    terminate();
                }

                cout << "--- [First Time Execution] Creating Initial Base File (" << filename_LBW << ") ---" << endl;

                //ヘッダー10バイトの書き込み
                unsigned char header_LBW[12];
                header_LBW[0] = (unsigned char)table_entry_size;
                header_LBW[1] = (unsigned char)N;
                header_LBW[2] = (unsigned char)L;
                header_LBW[3] = (unsigned char)B;
                header_LBW[4] = (unsigned char)W;

                size_t element_num_LBW = one_leaves_num_LBW;
                for(int i = 5; i < 12; i++){
                    header_LBW[i] = element_num_LBW % 256U;
                    element_num_LBW /= 256U;
                }
                ofs.write((char *)header_LBW, 12U);

                //昔の順次書き出し関数のロジックを応用した255（不明）埋め処理
                unsigned char m_buffer_LBW = 0U;
                unsigned int m_num_keep_LBW = 0;

                //ZDDのLの1葉経路数（要素数）の分だけ、ループを回してビットを詰めていく
                for (unsigned long long int i = 0; i < one_leaves_num_LBW; i++) {
                    unsigned int set_value = v_unknown;
                    Posi posi(i, zdd); if(L==0 && posi.Is_Init_Pos()){set_value = v_black;} //盤面フェンスが空でコマが初期位置なら初期配置として先手到達可能。
                    m_buffer_LBW |= static_cast<unsigned char>(set_value << (m_num_keep_LBW * table_entry_size));
    
                    //1バイト(8ビット)の中に要素が満タンになったらファイルへ1バイト書き出す
                    if (++m_num_keep_LBW >= (8U / table_entry_size)) {
                        ofs.write((char*)&m_buffer_LBW, 1U);
                        m_num_keep_LBW = 0;
                        m_buffer_LBW = 0U;
                    }
                }

                //フラッシュ処理
                //もしループ終了時に、1バイトに満たない端数のビットが残っていれば最後に書き出す
                if (m_num_keep_LBW != 0) {
                    ofs.write((char*)&m_buffer_LBW, 1U);
                }

                cout << "Base file generated successfully by sequential buffering logic." << endl;
                ofs.close();
            }
        }
        if(B > 0){
            // Lp1Bm1W側
            string filename_Lp1Bm1W = write_file_name_Lp1Bm1W;
            if (filename_Lp1Bm1W.empty() || filename_Lp1Bm1W == "NULL") {
                filename_Lp1Bm1W = "quoridor_reachable_N" + std::to_string(N) + "L" + std::to_string(L+1) + "B" + std::to_string(B-1) + "W" + std::to_string(W) + ".bin";
            }
            ifstream ifs(filename_Lp1Bm1W, ifstream::binary);
            //ファイルがすでに存在するかどうかチェックする（開ければ存在する）
            if (ifs) {
                // ファイルが存在する場合は何もしない（既存データを保護する）
                cout << "Initial file checking: " << filename_Lp1Bm1W << " already exists. Skip initialization." << endl;
                ifs.close();
            }
            else{
                //ここからファイル新規作成処理
                ofstream ofs(filename_Lp1Bm1W, ofstream::binary);
                if (!ofs) {
                    cerr << "Error: Cannot create initial base file: " << filename_Lp1Bm1W << endl;
                    terminate();
                }

                cout << "--- [First Time Execution] Creating Initial Base File (" << filename_Lp1Bm1W << ") ---" << endl;

                //ヘッダー10バイトの書き込み
                unsigned char header_Lp1Bm1W[12];
                header_Lp1Bm1W[0] = (unsigned char)table_entry_size;
                header_Lp1Bm1W[1] = (unsigned char)N;
                header_Lp1Bm1W[2] = (unsigned char)(L+1);
                header_Lp1Bm1W[3] = (unsigned char)(B-1);
                header_Lp1Bm1W[4] = (unsigned char)W;

                size_t element_num_Lp1Bm1W = one_leaves_num_Lp1Bm1W;
                for(int i = 5; i < 12; i++){
                    header_Lp1Bm1W[i] = element_num_Lp1Bm1W % 256U;
                    element_num_Lp1Bm1W /= 256U;
                }
                ofs.write((char *)header_Lp1Bm1W, 12U);

                //昔の順次書き出し関数のロジックを応用した255（不明）埋め処理
                unsigned char m_buffer_Lp1Bm1W = 0U;
                unsigned int m_num_keep_Lp1Bm1W = 0;

                //ZDDのLの1葉経路数（要素数）の分だけ、ループを回してビットを詰めていく
                for (unsigned long long int i = 0; i < one_leaves_num_Lp1Bm1W; i++) {
                    m_buffer_Lp1Bm1W |= static_cast<unsigned char>(v_unknown << (m_num_keep_Lp1Bm1W * table_entry_size));
    
                    //1バイト(8ビット)の中に要素が満タンになったらファイルへ1バイト書き出す
                    if (++m_num_keep_Lp1Bm1W >= (8U / table_entry_size)) {
                        ofs.write((char*)&m_buffer_Lp1Bm1W, 1U);
                        m_num_keep_Lp1Bm1W = 0;
                        m_buffer_Lp1Bm1W = 0U;
                    }
                }

                //フラッシュ処理
                //もしループ終了時に、1バイトに満たない端数のビットが残っていれば最後に書き出す
                if (m_num_keep_Lp1Bm1W != 0) {
                    ofs.write((char*)&m_buffer_Lp1Bm1W, 1U);
                }

                cout << "Base file generated successfully by sequential buffering logic." << endl;
                ofs.close();
            }
        }
        if(W > 0){
            // Lp1BWm1側
            string filename_Lp1BWm1 = write_file_name_Lp1BWm1;
            if (filename_Lp1BWm1.empty() || filename_Lp1BWm1 == "NULL") {
                filename_Lp1BWm1 = "quoridor_reachable_N" + std::to_string(N) + "L" + std::to_string(L+1) + "B" + std::to_string(B) + "W" + std::to_string(W-1) + ".bin";
            }
            ifstream ifs(filename_Lp1BWm1, ifstream::binary);
            //ファイルがすでに存在するかどうかチェックする（開ければ存在する）
            if (ifs) {
                // ファイルが存在する場合は何もしない（既存データを保護する）
                cout << "Initial file checking: " << filename_Lp1BWm1 << " already exists. Skip initialization." << endl;
                ifs.close();
            }
            else{
                //ここからファイル新規作成処理
                ofstream ofs(filename_Lp1BWm1, ofstream::binary);
                if (!ofs) {
                    cerr << "Error: Cannot create initial base file: " << filename_Lp1BWm1 << endl;
                    terminate();
                }

                cout << "--- [First Time Execution] Creating Initial Base File (" << filename_Lp1BWm1 << ") ---" << endl;

                //ヘッダー10バイトの書き込み
                unsigned char header_Lp1BWm1[12];
                header_Lp1BWm1[0] = (unsigned char)table_entry_size;
                header_Lp1BWm1[1] = (unsigned char)N;
                header_Lp1BWm1[2] = (unsigned char)(L+1);
                header_Lp1BWm1[3] = (unsigned char)B;
                header_Lp1BWm1[4] = (unsigned char)(W-1);

                size_t element_num_Lp1BWm1 = one_leaves_num_Lp1BWm1;
                for(int i = 5; i < 12; i++){
                    header_Lp1BWm1[i] = element_num_Lp1BWm1 % 256U;
                    element_num_Lp1BWm1 /= 256U;
                }
                ofs.write((char *)header_Lp1BWm1, 12U);

                //昔の順次書き出し関数のロジックを応用した255（不明）埋め処理
                unsigned char m_buffer_Lp1BWm1 = 0U;
                unsigned int m_num_keep_Lp1BWm1 = 0;

                //ZDDのLの1葉経路数（要素数）の分だけ、ループを回してビットを詰めていく
                for (unsigned long long int i = 0; i < one_leaves_num_Lp1BWm1; i++) {
                    m_buffer_Lp1BWm1 |= static_cast<unsigned char>(v_unknown << (m_num_keep_Lp1BWm1 * table_entry_size));
    
                    //1バイト(8ビット)の中に要素が満タンになったらファイルへ1バイト書き出す
                    if (++m_num_keep_Lp1BWm1 >= (8U / table_entry_size)) {
                        ofs.write((char*)&m_buffer_Lp1BWm1, 1U);
                        m_num_keep_Lp1BWm1 = 0;
                        m_buffer_Lp1BWm1 = 0U;
                    }
                }

                //フラッシュ処理
                //もしループ終了時に、1バイトに満たない端数のビットが残っていれば最後に書き出す
                if (m_num_keep_Lp1BWm1 != 0) {
                    ofs.write((char*)&m_buffer_Lp1BWm1, 1U);
                }

                cout << "Base file generated successfully by sequential buffering logic." << endl;
                ofs.close();
            }
        }
    }

    //ピンポイント上書きメソッド
    void Write_Elem_At(unsigned long long int table_id, unsigned int laabel) noexcept {
        string filename;
        if(table_id < one_leaves_num_LBW) {filename = m_filename_LBW;}
        else if(table_id < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W) {filename = m_filename_Lp1Bm1W;}
        else {filename = m_filename_Lp1BWm1;}

        if(table_id < one_leaves_num_LBW){table_id = table_id;}
        else if(table_id < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W){table_id = table_id - one_leaves_num_LBW;}
        else{table_id = table_id - one_leaves_num_LBW - one_leaves_num_Lp1Bm1W;}

        fstream fs(filename, fstream::in | fstream::out | fstream::binary);
        if (!fs) {
            cerr << "Error: Cannot open file for random write: " << filename << endl;
            terminate();
        }
        unsigned long long int total_bit_offset = table_id * table_entry_size;
        unsigned long long int byte_pos = 12ULL + (total_bit_offset / 8U);
        unsigned int bit_shift = total_bit_offset % 8U;

        fs.seekg(byte_pos, ios::beg);
        unsigned char current_byte = 0;
        fs.read((char*)&current_byte, 1U);

        unsigned char mask = ((1U << table_entry_size) - 1) << bit_shift;
        current_byte &= ~mask;
        current_byte |= (static_cast<unsigned char>(laabel) << bit_shift);

        fs.seekp(byte_pos, ios::beg);
        fs.write((char*)&current_byte, 1U);
        
        fs.close();
    } 
};

deque<Work *> deq_input;
deque<Work *> deq_output;
static void boss(const string &read_file_name_LBW, const string &read_file_name_Lp1Bm1W, const string &read_file_name_Lp1BWm1) noexcept {
    cout << "boss" << endl;
    OutTable_By_ThreeFile::Create_Initial_File(read_file_name_LBW, read_file_name_Lp1Bm1W, read_file_name_Lp1BWm1); //各分割の最初の実行において、基準となるファイルは存在しない。ここで作る。
    Table_By_ThreeFile table(read_file_name_LBW, read_file_name_Lp1Bm1W, read_file_name_Lp1BWm1);
    cout << "before new nblack = " << nblack << endl;
    cout << "before new nwhite = " << nwhite << endl;
    cout << "before new nboth = " << nboth << endl;
    cout << "before leave nunknown = " << nunknown << endl;
    
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
            //後退解析と違い、今回は不明じゃない配置を扱いたい。
            while(table.get(count_input) == v_unknown) {
                count_input++; 
                count_output++;
                if(count_input % 100000000ULL == 0ULL) cout << count_input << " " << nblack << " " << nwhite << " " << nboth << " " << nunknown << endl;
                //count_inputがlunknownを越えたらbreak
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
            if(count_input % 100000000ULL == 0ULL) cout << count_input << " " << nblack << " " << nwhite << " " << nboth << " " << nunknown << endl;
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
                bool new_child[one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1]; //この週で値が新しく着いたならtrueにする。
                fill(new_child, new_child + one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1, false); //新しいかどうかの配列初期化
                for (unsigned int workid = 0; workid < deq_tmp.size(); workid++) {
                    unsigned long long int id = deq_tmp[workid]->get_id();
                    unsigned int parent_value = table.get(id);
                    if(parent_value == v_unknown){continue;} // worker担当配置は不明なら何もしない。
                    
                    //デバッグ出力
                    //解析を進めるほど解析対象の数が膨れ上がるのでデバッグ出力を抑制しないと追跡しにくい。
                    //bool skip_debug = true;

                    int black_parent_num_child = deq_tmp[workid]->get_black_parent_num_child();
                    if((parent_value & 1) != 0 && black_parent_num_child > 0){
                        unsigned long long int array[black_parent_num_child];
                        deq_tmp[workid]->get_black_parent_array_id(black_parent_num_child, array);
                        /*
                        for(int childid = 0; childid < black_parent_num_child; childid++){
                            unsigned int child_value = table.get(array[childid]);
                            if(child_value == v_unknown || child_value == v_black){
                                skip_debug = false;
                            }
                        }
                        */
                    }
                    int m_white_parent_num_child = deq_tmp[workid]->get_white_parent_num_child();
                    if((parent_value & 2) != 0 && m_white_parent_num_child > 0){
                        unsigned long long int array[m_white_parent_num_child];
                        deq_tmp[workid]->get_white_parent_array_id(m_white_parent_num_child, array);
                        /*
                        for(int childid = 0; childid < m_white_parent_num_child; childid++){
                            unsigned int child_value = table.get(array[childid]);
                            if(child_value == v_unknown || child_value == v_white){
                                skip_debug = false;
                            }
                        }
                        */
                    }
                    
                    /*
                    Posi debug_posi(id, zdd);
                    if(!skip_debug){
                        if((parent_value & 1) != 0 && (parent_value & 2) == 0) cout << "parent is black. ";
                        else if((parent_value & 1) == 0 && (parent_value & 2) != 0) cout << "parent is white. ";
                        else if((parent_value & 1) != 0 && (parent_value & 2) != 0) cout << "parent is both. ";
                        cout << "parent id = " << id << endl;
                        debug_posi.View_Position();
                    }
                    */

                    //親が先手到達可能
                    if((parent_value & 1) != 0){
                        int black_parent_num_child = deq_tmp[workid]->get_black_parent_num_child();
                        if(black_parent_num_child > 0){

                            //if(!skip_debug) cout << "black parent child" << endl;

                            unsigned long long int array[black_parent_num_child];
                            deq_tmp[workid]->get_black_parent_array_id(black_parent_num_child, array);
                            for(int childid = 0; childid < black_parent_num_child; childid++){
                                unsigned int child_value = table.get(array[childid]); // ここでID違反が起きている

                                if(child_value == v_unknown){
                                    table.set_update(array[childid], v_white);
                                    nwhite++;
                                    nunknown--;
                                    new_child[array[childid]] = true;

                                    //cout << "child is unknown to white. " << "chilld id = " << array[childid] << endl;
                                    //debug_posi.make_posi(array[childid], zdd);
                                    //debug_posi.View_Position();

                                }
                                else if(child_value == v_black){
                                    table.set_update(array[childid], v_both);
                                    nboth++;
                                    if(new_child[array[childid]] == false){new_child[array[childid]] = true;}
                                    else{nblack--;}
                                    
                                    //cout << "child is black to both. " << "chilld id = " << array[childid] << endl;
                                    //debug_posi.make_posi(array[childid], zdd);
                                    //debug_posi.View_Position();
                                }
                                else{
                                    /*
                                    if(!skip_debug) {
                                        cout << "child is already defined. " << "chilld id = " << array[childid] << endl;
                                        debug_posi.make_posi(array[childid], zdd);
                                        debug_posi.View_Position();
                                    }
                                    */
                                }
                            }
                        }
                    }
                    //親が後手到達可能
                    if((parent_value & 2) != 0){
                        int m_white_parent_num_child = deq_tmp[workid]->get_white_parent_num_child();
                        if(m_white_parent_num_child > 0){
                            
                            //if(!skip_debug) cout << "white parent child" << endl;

                            unsigned long long int array[m_white_parent_num_child];
                            deq_tmp[workid]->get_white_parent_array_id(m_white_parent_num_child, array);
                            for(int childid = 0; childid < m_white_parent_num_child; childid++){
                                unsigned int child_value = table.get(array[childid]);
                                
                                if(child_value == v_unknown){
                                    table.set_update(array[childid], v_black);
                                    nblack++;
                                    nunknown--;
                                    new_child[array[childid]] = true;
                                    
                                    //cout << "child is unknown to black. " << "chilld id = " << array[childid] << endl;
                                    //debug_posi.make_posi(array[childid], zdd);
                                    //debug_posi.View_Position();
                                }
                                else if(child_value == v_white){
                                    table.set_update(array[childid], v_both);
                                    nboth++;
                                    if(new_child[array[childid]] == false){new_child[array[childid]] = true;}
                                    else{nwhite--;}
                                    
                                    //cout << "child is white to both. " << "chilld id = " << array[childid] << endl;
                                    //debug_posi.make_posi(array[childid], zdd);
                                    //debug_posi.View_Position();
                                }
                                else{
                                    /*
                                    if(!skip_debug) {
                                        cout << "child is already defined. " << "chilld id = " << array[childid] << endl;
                                        debug_posi.make_posi(array[childid], zdd);
                                        debug_posi.View_Position();
                                    }
                                    */
                                }
                            }
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
    
    OutTable_By_ThreeFile out_table(read_file_name_LBW, read_file_name_Lp1Bm1W, read_file_name_Lp1BWm1);
    unsigned long long int update_counter = 0;
    for (unsigned long long int i = 0; i < one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1; i++) {
        unsigned int get_reach = table.get(i);
        if (table.is_changed(i)) { 
            out_table.Write_Elem_At(i, get_reach);
            update_counter++;
        }
    }
    cout << "File overwrite completed. Total " << update_counter << " elements updated." << endl;

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
            unsigned int black_actions[3000]; //親が先手の時の可能手
            int nchild = posi.compute_actions(black_actions, B>0); // posiの合法手列挙。actionsに可能手をすべて記録し可能手数を返す。
            if(nchild > 0) { // もし合法手があれば子節点の整数値列挙
                unsigned long long int child_array_id[nchild];
                int skip_count = 0; //可能手によって終端節点に行くなら含めない。
                for(int childid = 0; childid < nchild; childid++) {
                    posi.make_action(black_actions[childid]); 
                    //ゴール配置はZDDに含まれないので飛ばす。
                    if(posi.Is_Black_Goal()){
                        skip_count++;
                    }else{
                        if(black_actions[childid] < N*N){
                            child_array_id[childid-skip_count] = posi.compute_id(zdd); //反転せずにそのまま使う
                        }
                        else{
                            child_array_id[childid-skip_count] = posi.compute_id(zdd) - one_leaves_num_LBW + one_leaves_num_LBW;
                        }
                    }
                    posi.unmake_action(black_actions[childid]);
                }
                w->set_black_parent_array_id(nchild-skip_count, child_array_id);
            } 
            else if (nchild == 0) { //何もできない配置。
                unsigned long long int child_array_id[3000];
                w->set_black_parent_array_id(0, child_array_id);
            }
        }
        else{    
            unsigned long long int child_array_id[3000];
            w->set_black_parent_array_id(0, child_array_id);
        }
        //親が後手到達可能なら盤面を反転。これで後手視点に。そこから子配置を作り再び反転。それが先手到達可能配置。
        if((parent & 2) != 0){ //2bit目が1なら親配置は後手到達可能
            unsigned int white_actions[3000]; //親が後手の時の可能手
            unsigned long long int flip_id = posi.compute_id_flip(zdd);
            Posi flip_posi(flip_id, zdd);
            int nchild = flip_posi.compute_actions(white_actions, W>0);
            if(nchild > 0) { // もし合法手があれば子節点の整数値列挙
                unsigned long long int child_array_id[nchild];
                int skip_count = 0; //可能手によって終端節点に行くなら含めない。
                for(int childid = 0; childid < nchild; childid++) {
                    //反転した先手、つまり後手の行動を反映
                    flip_posi.make_action(white_actions[childid]); 
                    if(flip_posi.Is_Black_Goal()){
                        skip_count++;
                    }else{
                        //子配置には、後手行動後再び反転した配置を入れる必要有。
                        if(white_actions[childid] < N*N){
                            child_array_id[childid-skip_count] = flip_posi.compute_id_flip(zdd);
                        }
                        else{
                            child_array_id[childid-skip_count] = flip_posi.compute_id_flip(zdd) - one_leaves_num_LBW + one_leaves_num_LBW + one_leaves_num_Lp1Bm1W; 
                        }
                    }
                    flip_posi.unmake_action(white_actions[childid]);
                }
                w->set_white_parent_array_id(nchild-skip_count, child_array_id);
            } else if (nchild == 0) { //何もできない配置。
                unsigned long long int child_array_id[3000];
                w->set_white_parent_array_id(0, child_array_id);
            }
        }
        else{
            unsigned long long int child_array_id[3000];
            w->set_white_parent_array_id(0, child_array_id);
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
    one_leaves_num = zdd.One_Leaves_Num();
    one_leaves_num_L = zdd.One_Leaves_Num_L();
    one_leaves_num_Lp1 = zdd.One_Leaves_Num_Lp1();
    
    one_leaves_num_LBW = one_leaves_num_L;
    one_leaves_num_Lp1Bm1W = B > 0 ? one_leaves_num_Lp1 : 0;
    one_leaves_num_Lp1BWm1 = W > 0 ? one_leaves_num_Lp1 : 0;

    table_size64 = (one_leaves_num_LBW + one_leaves_num_Lp1Bm1W + one_leaves_num_Lp1BWm1 + 64ULL/table_entry_size - 1) / (64ULL/table_entry_size);
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

    cout << "new nblack  =  " << nblack << endl;
    cout << "new nwhite  =  " << nwhite << endl;
    cout << "new nboth  =  " << nboth << endl;
    cout << "leave nunknown  =  " << nunknown << endl; //残り不明配置数。
    cout << nblack + nwhite + nboth << endl;
    cout << nunknown << endl; 
    return 0;
}
