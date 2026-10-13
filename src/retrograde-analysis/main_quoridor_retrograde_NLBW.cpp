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

// 盤面サイズN、盤上フェンスL、先手手持ちB、後手手持ちWを受け取ることで後退解析を行う。
// 使うファイルはNLBWをファイル名に入れる必要がある。

ZDD zdd;
unsigned int table_entry_size = 8U;

unsigned long long int one_leaves_num = 0ULL; //ZDD全体の1葉配置数をここに入れる。
unsigned long long int one_leaves_num_L = 0ULL; //ZDDのうち盤面フェンス数Lの1葉配置数をここに入れる。
unsigned long long int one_leaves_num_Lp1 = 0ULL; //ZDDのうち盤面フェンス数L+1の1葉配置数をここに入れる。

// 先手後手手持ち枚数が異なる場合、その手持ちを反転させた表を見る必要がある。
// 後退解析では子配置は先手行動後の反転配置
unsigned long long int one_leaves_num_LBW = 0ULL; //解析対象（LWBでコマを動かしたときに参照する子配置）
unsigned long long int one_leaves_num_LWB = 0ULL; //解析対象（LBWでコマを動かしたときに参照する子配置）
unsigned long long int one_leaves_num_Lp1WBm1 = 0ULL; //LBWでフェンスを使った時に参照する子配置
unsigned long long int one_leaves_num_Lp1BWm1 = 0ULL; //LWBでフェンスを使った時に参照する子配置

unsigned long long int table_size64 = 0ULL; //uint64を8ビットエントリにした時のゲーム値表のサイズ。
constexpr int nworker = 1; //分割数（特に指定はない。後で調整。）
constexpr int deq_input_size = 1024; //分割したスレッドに渡すデータのサイズ（とりあえずこれ。後で調整。）
constexpr int deq_output_size = 256; //スレッドが返す結果のデータのサイズ（とりあえずこれ。後で調整。）

// main引数で渡す先手後手フェンス組合せ
int L = 0;
int B = 0;
int W = 0;
int ITER = 0;

using namespace std;

//並列計算の同期用。
//mutexは複数のスレッドで1つの処理を実行する場合、排他的に行うようにする。
//condition_variableはスレッドAがスレッドBの結果を使用したいとき、その結果の待機通信で使用する。mutexとセットで使う。
condition_variable cv_boss;
condition_variable cv_worker;
mutex mtx;

unsigned long long int nwin = 0ULL, nlose = 0ULL, nunknown = 0ULL, lunknown; //勝ち配置数、負け配置数、不明配置数、不明配置の最大ID（ZDD1葉配置数-1）
bool flag_worker_quit = false; //仕事が終わったことを表すフラグ

unsigned long long int length = 0; //keiroのサイズ-1は最大経路長。経路長の分布を調べる用。使っていないのでいったん無視。

//ファイル命名規則quoridor_retrograde_NLBW（盤面サイズ、盤上フェンス、先手手持ち、後手手持ち）
//解析対象ファイルはNLBWとNLWBの2つのファイルを入力として扱う（BWの手持ち反転したものがWB）
//解析の際に参照するためにN(L+1)W(B-1)とN(L+1)B(W-1)の2つが追加で必要。
class InTable_By_FourFile{
private:
    unsigned char m_buffer;
    unsigned int m_num_keep;
    unsigned long long int m_total_read_num = 0; //これまで読み込んだ要素数が一つ目のファイルの表のサイズを越えたら二つ目のファイルを読み込む
    ifstream m_ifs_LBW;
    ifstream m_ifs_LWB;
    ifstream m_ifs_Lp1WBm1;
    ifstream m_ifs_Lp1BWm1;
public:
    InTable_By_FourFile() = delete; //引数なしコンストラクタを禁止する。
    //呼び出すコンストラクタは必ずこっち。ZDD構築後に呼び出すこと。
    InTable_By_FourFile(const string &read_file_name_LBW, const string &read_file_name_LWB, const string &read_file_name_Lp1WBm1, const string &read_file_name_Lp1BWm1) noexcept 
    : m_buffer(0), m_num_keep(0){
        //引数整合性チェック
        //ファイルが必要か否か
        {
            // 後退解析の反復数は結果に関係ないのでファイルを重複して入力することを許す
            // むしろ1回の反復で2回解析入るから効率的ではないか
            if(read_file_name_LBW.empty() || read_file_name_LBW == "NULL"){
                cerr << "Input file LBW is always necessary" << endl;
                terminate();
            }
            if(read_file_name_LWB.empty() || read_file_name_LWB == "NULL"){
                cerr << "Input file LWB is always necessary" << endl;
                terminate();
            }
            if(B > 0 && (read_file_name_Lp1WBm1.empty() || read_file_name_Lp1WBm1 == "NULL")){
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
        //LWBファイル読み込み
        {
            unsigned char header_LWB[12];
            size_t elem_num_check_LWB = 0ULL;
            m_ifs_LWB.open(read_file_name_LWB, ifstream::binary);
            if(!m_ifs_LWB){
                cerr << "Input file LWB do not exist" << endl;
                terminate();
            }
            else {       
                //ヘッダー読み込み
                cout << "read header LWB: ";
                for(int i = 0; i < 12; i++){
                    m_ifs_LWB.read((char*)header_LWB + i, 1U);
                    cout << (int)header_LWB[i] << " ";
                }
                cout << endl;

                //ヘッダー情報照合
                if(header_LWB[0] != table_entry_size){
                    cerr << "Entry size error in LBW_flip file" << endl;
                    terminate();
                }
                if(header_LWB[1] != (unsigned char)N || header_LWB[2] != (unsigned char)L || header_LWB[3] != (unsigned char)W || header_LWB[4] != (unsigned char)B){
                    cerr << "Invalid (game setting N/L/W/B) : (file setting N/L/W/B) = " << "(" << N << "/" << L << "/" << W << "/" << B << ")" << " : " << "(" << (int)header_LWB[1] << "/" << (int)header_LWB[2] << "/" << (int)header_LWB[3] << "/" << (int)header_LWB[4] << ")" << endl;
                    terminate(); 
                }
                for(int i = 7; i > 0; i--){ //ファイルでは配置数を256進数で表現されているので元に戻す。
                    elem_num_check_LWB *= 256ULL;
                    elem_num_check_LWB += header_LWB[i+4];
                }
                if(elem_num_check_LWB != one_leaves_num_LWB) {
                    cerr << "Size Error" << endl;
                    terminate();
                }
            }
        }
        //Lp1WBm1ファイル読み込み
        if(B > 0){
            unsigned char header_Lp1WBm1[12];
            size_t elem_num_check_Lp1WBm1 = 0ULL;
            m_ifs_Lp1WBm1.open(read_file_name_Lp1WBm1, ifstream::binary);
            if(!m_ifs_Lp1WBm1){
                cerr << "Input file Lp1WBm1 do not exist" << endl;
                terminate();
            }
            else {       
                cout << "read header Lp1WBm1: ";
                for(int i = 0; i < 12; i++){
                    m_ifs_Lp1WBm1.read((char*)header_Lp1WBm1 + i, 1U);
                    cout << (int)header_Lp1WBm1[i] << " ";
                }
                cout << endl;

                //ヘッダー情報照合
                if(header_Lp1WBm1[0] != table_entry_size){
                    cerr << "Entry size error in Lp1WBm1 file" << endl;
                    terminate();
                }
                if(header_Lp1WBm1[1] != (unsigned char)N || header_Lp1WBm1[2] != (unsigned char)(L+1) || header_Lp1WBm1[3] != (unsigned char)W || header_Lp1WBm1[4] != (unsigned char)(B-1)){
                    cerr << "Invalid (game setting N/(L+1)/W/(B-1)) : (file setting N/(L+1)/W/(B-1)) = " << "(" << N << "/" << L+1 << "/" << W << "/" << B-1 << ")" << " : " << "(" << (int)header_Lp1WBm1[1] << "/" << (int)header_Lp1WBm1[2] << "/" << (int)header_Lp1WBm1[3] << "/" << (int)header_Lp1WBm1[4] << ")" << endl;
                    terminate(); 
                }
                for(int i = 7; i > 0; i--){ //ファイルでは配置数を256進数で表現されているので元に戻す。
                    elem_num_check_Lp1WBm1 *= 256ULL;
                    elem_num_check_Lp1WBm1 += header_Lp1WBm1[i+4];
                }
                if(elem_num_check_Lp1WBm1 != one_leaves_num_Lp1WBm1) {
                    cerr << "Size Error" << endl;
                    terminate();
                }
            }
        }
        //Lp1BWm1ファイル読み込み
        if(W > 0){
            unsigned char header_Lp1BWm1[12];
            size_t elem_num_check_Lp1BWm1 = 0ULL;
            m_ifs_Lp1BWm1.open(read_file_name_Lp1BWm1, ifstream::binary);
            if(!m_ifs_Lp1BWm1){
                cerr << "Input file Lp1BWm1 do not exist" << endl;
                terminate();
            }
            else {     
                cout << "read header Lp1BWm1: ";  
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
    ~InTable_By_FourFile() noexcept {
        m_ifs_LBW.close();
        m_ifs_LWB.close();
        m_ifs_Lp1WBm1.close();
        m_ifs_Lp1BWm1.close();
    }
    unsigned int Order_Read_One_Elem() noexcept { //要素を1つ目のファイルから順に1つずつ取り出していく。引数の順番にファイルを読み込む。
        //読み込みファイルが切り替わった時を検知。1エントリ8ビットの現在では無意味。
        if (m_total_read_num == one_leaves_num_LBW || 
            m_total_read_num == one_leaves_num_LBW + one_leaves_num_LWB ||
            m_total_read_num == one_leaves_num_LBW + one_leaves_num_LWB + one_leaves_num_Lp1WBm1) {
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
                    m_buffer = (1U << table_entry_size) - 1;
                }
            }
            else if(m_total_read_num < one_leaves_num_LBW + one_leaves_num_LWB){
                if(m_ifs_LWB){
                    m_ifs_LWB.read((char*)&m_buffer, 1U);
                }
                else{
                    m_buffer = (1U << table_entry_size) - 1;
                }
            }
            else if(m_total_read_num < one_leaves_num_LBW + one_leaves_num_LWB + one_leaves_num_Lp1WBm1){
                if(m_ifs_Lp1WBm1){
                    m_ifs_Lp1WBm1.read((char*)&m_buffer, 1U);
                }
                else{
                    m_buffer = (1U << table_entry_size) - 1;
                }
            }
            else{
                if(m_ifs_Lp1BWm1){
                    m_ifs_Lp1BWm1.read((char*)&m_buffer, 1U);
                }
                else{
                    m_buffer = (1U << table_entry_size) - 1;
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

//InTable_By_TwoFileを定義してバイナリファイルを読み込む。
//読み書きの制御。
class Table_By_FourFile{
private:
    uint64_t *m_table; //ゲーム値書き込み表

    uint64_t *m_changed_flags; //更新フラグ管理表（1エントリ = 1ビット）
    size_t m_flags_size64; //更新フラグ管理表サイズ

public:
    Table_By_FourFile(const string &read_file_name_LBW, const string &read_file_name_LWB, const string &read_file_name_Lp1WBm1, const string &read_file_name_Lp1BWm1) noexcept 
    : m_table(new uint64_t [table_size64]), m_changed_flags(nullptr){

        m_flags_size64 = (one_leaves_num_LBW + one_leaves_num_LWB + 64ULL - 1ULL) / 64ULL;
        m_changed_flags = new uint64_t[m_flags_size64];
        //更新フラグ初期化。全て更新されていない状態にする。
        for(size_t i = 0; i < m_flags_size64; i++){
            m_changed_flags[i] = 0ULL;
        }

        cout << "read file name LBW = " << read_file_name_LBW << endl;
        cout << "read file name LWB = " << read_file_name_LWB << endl;
        cout << "read file name Lp1WBm1 = " << read_file_name_Lp1WBm1 << endl;
        cout << "read file name Lp1BWm1 = " << read_file_name_Lp1BWm1 << endl;

        InTable_By_FourFile in_table_by_fourfile(read_file_name_LBW, read_file_name_LWB, read_file_name_Lp1WBm1, read_file_name_Lp1BWm1); //長い数字は1葉配置数に変える。
        //メモリにゲーム値表を作成
        //LBW、LWBどっちの配置の勝敗か分かるようにする
        unsigned long long int temp_nwin = 0ULL;
        unsigned long long int temp_nlose = 0ULL;
        unsigned long long int temp_nunknown = 0ULL;
        for (unsigned long long int i = 0; i < one_leaves_num_LBW + one_leaves_num_LWB + one_leaves_num_Lp1WBm1 + one_leaves_num_Lp1BWm1; i++) {
            unsigned int v = in_table_by_fourfile.Order_Read_One_Elem();
            //盤面フェンス数Lの配置に対してのみ、勝敗結果の数を記録。
            if(i < one_leaves_num_LBW + one_leaves_num_LWB){
                if(i == one_leaves_num_LBW){
                    cout << "LBW table" << endl;
                    cout << "table LBW nwin  =  " << temp_nwin << endl;
                    cout << "table LBW nlose  =  " << temp_nlose << endl;
                    cout << "table LBW nunknown  =  " << temp_nunknown << endl;
                    temp_nwin = 0ULL;
                    temp_nlose = 0ULL;
                    temp_nunknown = 0ULL;
                }
                if(v == (1ULL << table_entry_size) - 1) {
                    nunknown++;
                    temp_nunknown++;
                    lunknown = i;
                }
                else if(v%2 == 1) {
                    temp_nwin++;
                    nwin++;
                }
                else if(v%2 == 0) {
                    temp_nlose++;
                    nlose++;
                } 
            }
            set_only(i, v);
        }
        
        cout << "LWB table" << endl;
        cout << "table LWB nwin  =  " << temp_nwin << endl;
        cout << "table LWB nlose  =  " << temp_nlose << endl;
        cout << "table LWB nunknown  =  " << temp_nunknown << endl;

        cout << "total table" << endl;
        cout << "table nwin  =  " << nwin << endl;
        cout << "table nlose  =  " << nlose << endl;
        cout << "table nunknown  =  " << nunknown << endl;
        cout << "last unknown  =  " << lunknown << endl;

        //新しく判明した勝敗を実行後出力したい。
        nwin = 0; //ここで勝ち配置数を0にする。
        nlose = 0; //ここで負け配置数を0にする。
    }
    ~Table_By_FourFile() noexcept {
        delete [] m_table;
        delete [] m_changed_flags;
    } //メモリ解法

    //指定したエントリサイズの表から値を取り出す。表はULL配列なのでエントリサイズに対応させる。
    unsigned int get(unsigned long long int table_id) const noexcept {
        unsigned long long int id64 = table_id / (64ULL/table_entry_size); //ULL配列の何番目の要素にアクセスするか
        unsigned long long int id1 = (table_id % (64ULL/table_entry_size)) * table_entry_size; //アクセスした要素の何ビット目から読み込むか
        unsigned int v = ((1ULL << table_entry_size) - 1) & (unsigned int)(m_table[id64] >> (64ULL-id1-table_entry_size)); //取り出したい部分を下位エントリサイズビットに入るよう左シフトしマスクして取り出す。
        return v;
    }
    
    //指定したエントリサイズの表に値を入れる。表はULL配列なのでエントリサイズに対応させる。
    //後退解析中に値が書き換わったら、対応するビットに1を立てる。
    //更新するのはLファイルのみの制限をかける。
    void set_update(unsigned long long int table_id, unsigned int write_element) noexcept {
        if(table_id >= one_leaves_num_LBW + one_leaves_num_LWB){
            cerr << "cannot set update. ID mismatch." << endl;
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
        if(table_id >= one_leaves_num_LBW + one_leaves_num_LWB){
            cerr << "cannot is change check. ID mismatch." << endl;
            terminate(); 
        }

        unsigned long long int id64 = table_id / 64ULL;       // uint64_t配列のインデックス
        unsigned long long int bit_pos = table_id % 64ULL;   // uint64_t内のビット位置 (0〜63)
        
        // 指定したビットが 1 であれば true（更新済み）を返す
        return (m_changed_flags[id64] & (1ULL << (63ULL - bit_pos))) != 0ULL;
    }
};

//並列化で使う。スレッドでやり取り。
//bossとworkerはこのworkを通して後退解析の情報のやり取りをする。
class Work {
private:
    unsigned long long int m_id; //担当している配置のID
    int m_path_length; // 初期配置から担当配置までの経路長。
    int m_num_child; //担当している配置からの可能手数。
    unsigned long long int m_array_id[3000]; //可能手後の子配置ID。
                                             //Quoridorだと最大フェンス盤面8*8、最大フェンス番号20、最大コマ盤面9*9。8*8*2*20 + 81 = 2560 + 81 = 2641。3000で十分。

public:
    Work() noexcept {}
    Work(unsigned long long int id) noexcept : m_id(id) {} //IDを指定して設定するコンストラクタ

    //パラメータを設定する関数群
    void set_id(unsigned long long int id) noexcept { m_id = id;}
    void set_path_length(int path_length) noexcept { m_path_length = path_length; }
    void set_array_id(int num_child, unsigned long long int array_id[]) noexcept {
        m_num_child = num_child;
        if(num_child <= 0) return;
        for(int childid = 0; childid < m_num_child; childid++) {
            m_array_id[childid] = array_id[childid];
        }
    }

    //パラメータを返す関数群
    //配列は引数にパラメータを代入する形で返す
    unsigned long long int get_id() const noexcept { return m_id; }
    int get_path_length() const noexcept { return m_path_length; }
    int get_num_child() const noexcept { return m_num_child; }
    void get_array_id(int num_child, unsigned long long int array_id[]) const noexcept {
        for(int childid = 0; childid < m_num_child; childid++) {
            array_id[childid] = m_array_id[childid];
        }
    }
};

//後退解析の結果を書き込む。
//盤面フェンス数Lのファイルのみが書き換わるので注意。
class OutTable_By_FourFile {
private:
    string m_filename_LBW;
    string m_filename_LWB;

public:
    OutTable_By_FourFile() = delete;
    
    //コンストラクタ自身ではファイルの有無の判定やヘッダーの自動生成は行わない。
    //後退解析実行前にファイルが存在しないならヘッダーと不明で埋められたファイルを作るものとする。
    OutTable_By_FourFile(const string &write_file_name_LBW, const string &write_file_name_LWB) noexcept {
        m_filename_LBW = write_file_name_LBW;
        m_filename_LWB = write_file_name_LWB;
    }

    ~OutTable_By_FourFile() noexcept {}

    //後退解析の前に呼び出す、土台ファイル作成用の静的関数
    //ファイルが存在しない（またはオープンできない）場合のみ、確実にヘッダーと勝敗不明データを書き込み
    static void Create_Initial_File(const string &write_file_name_LBW, const string &write_file_name_LWB) noexcept {
        //生成ファイル名定義
        {
            // LBW側
            string filename_LBW = write_file_name_LBW;
            if (filename_LBW.empty() || filename_LBW == "NULL") {
                filename_LBW = "quoridor_gamevalue_N" + std::to_string(N) + "L" + std::to_string(L) + "B" + std::to_string(B) + "W" + std::to_string(W) + ".bin";
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
                unsigned char m_buffer = 0U;
                unsigned int m_num_keep = 0;

                unsigned int unknown_value = (1U << table_entry_size) - 1; 

                //ZDDのLの1葉経路数（要素数）の分だけ、ループを回してビットを詰めていく
                for (unsigned long long int i = 0; i < one_leaves_num_LBW; i++) {
                    m_buffer |= static_cast<unsigned char>(unknown_value << (m_num_keep * table_entry_size));
    
                    //1バイト(8ビット)の中に要素が満タンになったらファイルへ1バイト書き出す
                    if (++m_num_keep >= (8U / table_entry_size)) {
                        ofs.write((char*)&m_buffer, 1U);
                        m_num_keep = 0;
                        m_buffer = 0U;
                    }
                }

                //フラッシュ処理
                //もしループ終了時に、1バイトに満たない端数のビットが残っていれば最後に書き出す
                if (m_num_keep != 0) {
                    ofs.write((char*)&m_buffer, 1U);
                }

                cout << "Base file generated successfully by sequential buffering logic." << endl;
                ofs.close();
            }
        }
        {
            // LWB側
            string filename_LWB = write_file_name_LWB;
            if (filename_LWB.empty() || filename_LWB == "NULL") {
                filename_LWB = "quoridor_gamevalue_N" + std::to_string(N) + "L" + std::to_string(L) + "B" + std::to_string(W) + "W" + std::to_string(B) + ".bin";
            }
            ifstream ifs(filename_LWB, ifstream::binary);
            //ファイルがすでに存在するかどうかチェックする（開ければ存在する）
            if (ifs) {
                // ファイルが存在する場合は何もしない（既存データを保護する）
                cout << "Initial file checking: " << filename_LWB << " already exists. Skip initialization." << endl;
                ifs.close();
            }
            else{
                //ここからファイル新規作成処理
                ofstream ofs(filename_LWB, ofstream::binary);
                if (!ofs) {
                    cerr << "Error: Cannot create initial base file: " << filename_LWB << endl;
                    terminate();
                }

                cout << "--- [First Time Execution] Creating Initial Base File (" << filename_LWB << ") ---" << endl;

                //ヘッダー10バイトの書き込み
                unsigned char header_LWB[12];
                header_LWB[0] = (unsigned char)table_entry_size;
                header_LWB[1] = (unsigned char)N;
                header_LWB[2] = (unsigned char)L;
                header_LWB[3] = (unsigned char)W;
                header_LWB[4] = (unsigned char)B;

                size_t element_num_LWB = one_leaves_num_LWB;
                for(int i = 5; i < 12; i++){
                    header_LWB[i] = element_num_LWB % 256U;
                    element_num_LWB /= 256U;
                }
                ofs.write((char *)header_LWB, 12U);

                //昔の順次書き出し関数のロジックを応用した255（不明）埋め処理
                unsigned char m_buffer = 0U;
                unsigned int m_num_keep = 0;

                unsigned int unknown_value = (1U << table_entry_size) - 1; 

                //ZDDのLの1葉経路数（要素数）の分だけ、ループを回してビットを詰めていく
                for (unsigned long long int i = 0; i < one_leaves_num_LWB; i++) {
                    m_buffer |= static_cast<unsigned char>(unknown_value << (m_num_keep * table_entry_size));
    
                    //1バイト(8ビット)の中に要素が満タンになったらファイルへ1バイト書き出す
                    if (++m_num_keep >= (8U / table_entry_size)) {
                        ofs.write((char*)&m_buffer, 1U);
                        m_num_keep = 0;
                        m_buffer = 0U;
                    }
                }

                //フラッシュ処理
                //もしループ終了時に、1バイトに満たない端数のビットが残っていれば最後に書き出す
                if (m_num_keep != 0) {
                    ofs.write((char*)&m_buffer, 1U);
                }

                cout << "Base file generated successfully by sequential buffering logic." << endl;
                ofs.close();
            }
        }
    }

    //ピンポイント上書きメソッド
    void Write_Elem_At(unsigned long long int table_id, unsigned int steps) noexcept {
        string filename = table_id < one_leaves_num_LBW ? m_filename_LBW : m_filename_LWB;
        table_id = table_id < one_leaves_num_LBW ? table_id : (table_id - one_leaves_num_LBW); // ファイルに書き込むIDはずらす。
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
        current_byte |= (static_cast<unsigned char>(steps) << bit_shift);

        fs.seekp(byte_pos, ios::beg);
        fs.write((char*)&current_byte, 1U);
        
        fs.close();
    } 
};

//bossとworker間
deque<Work *> deq_input; //bossからworkerに勝敗不明の配置を渡す時に使う。
deque<Work *> deq_output; //workerからbossに解析後の配置を渡す時に使う。

//WorkerへのID割り振りと、Workerからもらった配置IDを基にゲーム値表を更新。
static void boss(const string &read_file_name_LBW, const string &read_file_name_LWB, const string &read_file_name_Lp1WBm1, const string &read_file_name_Lp1BWm1) noexcept {
    cout << "boss" << endl;
    //OutTable_By_TwoFile::Create_Initial_File(read_file_name_L); //各分割の最初の実行において、基準となるLファイルは存在しない。ここで作る。
    OutTable_By_FourFile::Create_Initial_File(read_file_name_LBW, read_file_name_LWB);
    //Table_By_TwoFile table(read_file_name_L, read_file_name_Lp1);
    Table_By_FourFile table(read_file_name_LBW, read_file_name_LWB, read_file_name_Lp1WBm1, read_file_name_Lp1BWm1);
    cout << "before new nwin  =  " << nwin << endl;
    cout << "before new nlose  =  " << nlose << endl;
    cout << "before leave nunknown  =  " << nunknown << endl;
    
    unsigned long long int count_input = 0ULL; //workerに割り振った配置数。これを基準に表から配置を参照する。
    unsigned long long int count_output = 0ULL; //workerから受け取った配置数。これを基準に表から配置を参照する。
    int nstack_work_idle = deq_input_size + deq_output_size + nworker; //必要なworkの最大数。workerで仕事している時入出力のキューから仕事が取り除かれている。入出力キューのサイズと並列数の合計がスタックの最大使用数。
    Work* stack_work_idle[nstack_work_idle]; //空いているworkを格納する。
    for(int i = 0; i < nstack_work_idle; i++) stack_work_idle[i] = new Work; //workのメモリ確保
    
    while (true) {// 仕事がなくなるまで繰り返す。不明配置について解析し終わったら終了。
        unique_lock<mutex> lck(mtx); //muutexによるロック。複数のスレッドが1つの処理を排他的に処理する。
        cv_boss.wait(lck, [&](){
            return (((deq_input.size() < deq_input_size) && (deq_output.size() < deq_output_size) && (count_input <= lunknown))
                    || (0 < deq_output.size())); }); //condition_variable.wait(mutex.lock(), [&](){return ~;});ラムダ式内がtrueになるまで待機。
                                                     //「workを入れるキューに空きがあり、まだ解析されていない不明配置が存在する」または「仕事の結果が少なくとも1つかえってきた」場合に起きる。
        //ロックの解放とsleepはアトミック（不可分）なので同時。
        //起きたなら、ロックは獲得している。他のスレッドは介入できない。
        //起きてから式の評価を1回行い、偽ならばロックを解放して再び寝る。真ならば、コールバック。
        if ((deq_input.size() < deq_input_size) && (deq_output.size() < deq_output_size) && count_input <= lunknown) { //「workを入れるキューに空きがあり、まだ解析されていない不明配置が存在する」
            //後退解析の表を使う。
            while(table.get(count_input) != (1U << table_entry_size)- 1) { //不明以外の値が付与されているのなら出力済みとしてカウントを進める。入力カウントが不明配置の最大IDを越えたらループを抜ける。
                count_input++; 
                count_output++;
                if(count_input % 100000000ULL == 0ULL) cout << count_input << " " << nwin << " " << nlose << " " << nunknown << endl;
                //count_inputがlunknownを越えたらbreak
                if(count_input > lunknown){break;}
            }
            //count_inputだけがlunknownを越えていたらworkerが不明配置すべて扱い終わるまでcontinueでループして待機。
            //count_outputも越えていたら仕事を与え終わってかつそれらが終了したことになるのでbreak。
            if(count_input > lunknown){
                if(count_output > lunknown){break;}
                else{continue;}
            }
            assert(table.get(count_input) == (1U << table_entry_size)- 1); //不明配置がくるまでスキップしてきたのでループを抜けて最後に見たところが不明である必要がある。
            assert(nstack_work_idle >= 1); //空のworkが存在している必要がある。
            Work *pw = stack_work_idle[ --nstack_work_idle ]; //空いているworkのうち最後尾を取り出す。スタック数をデクリメント。
            pw->set_id(count_input); //空きworkを入力workにするためにIDを与える。count_inputは不明IDになるまでスキップしている。
            deq_input.push_front(pw); //worker向けに新しい仕事を追加する。
            lck.unlock(); //ロック解放。workerがキューに触れるようになる。
            count_input++; //入力work数をインクリメント。
            if(count_input % 100000000ULL == 0ULL) cout << count_input << " " << nwin << " " << nlose << " " << nunknown << endl;
            cv_worker.notify_one(); //sleep状態のworkerスレッドを1つ起こして仕事させる。
        } else { //キューに空きがない（仕事割り振りスペースがないかこれ以上結果を受け取れない）、あるいは最後の配置まで解析が終わっていないなら
            if (0 < deq_output.size()) { //workerから結果が少なくとも1つ帰ってきている。
                assert(0 < deq_output.size()); //分岐時点で満たしている。
                deque<Work *> deq_tmp; //一時的なキュー
                swap(deq_tmp, deq_output); //workerからの結果と一時的なキューの中身を入れ替える。スレッド使用時間を最小限にするためらしい。
                deq_output.clear(); //今入っているworkerからの結果をクリアにする。
                lck.unlock(); //一時的なキューに処理対象の結果を保存したので他のworkerが結果を出力しても良い。ロック解放。
                count_output += deq_tmp.size(); //終了タスク数を一時的なキューのサイズ分加算。
                for (unsigned int workid = 0; workid < deq_tmp.size(); workid++) { //処理対象の配置に勝敗を付与する。
                    unsigned long long int id = deq_tmp[workid]->get_id(); //配置のIDを取得
                    int nchild = deq_tmp[workid]->get_num_child(); //配置からの可能手数を取得
                    if (nchild > 0) { //可能手が存在するなら
                        //子配置情報から参照配置のラベルはを決定するためのラベル。
                        bool is_win = false;
                        bool is_unknown = false;
                        int min_win_steps = -1;  //もし参照配置が先手勝ちであるなら、子配置のうち先手負けに繋がる最小手数を登録。
                        int max_lose_steps = -1; //もし参照配置が先手負けであるなら、子配置のうち先手勝ちに繋がる最大手数を登録。
                        unsigned long long int array[nchild]; //子配置IDを格納する配列。必ず先手から見た盤面が返される。
                        deq_tmp[workid]->get_array_id(nchild, array); //子配置ID格納。
                        
                        //0番目に配置IDではなく特殊IDのULLONG_MAX-1があるなら1手勝ち可能な状態。
                        //table.getしようとするとULLONGMAX-1がIDと誤認されてメモリアクセスエラーでクラッシュするので最初にチェック。
                        if(array[0] == ULLONG_MAX-1){
                            is_win = true;
                            min_win_steps = 0;
                            //if(L == 2) cout << "black win pos" << endl;
                        }
                        //参照しているのが1手勝ち配置でないなら子配置をすべて見る。
                        else{
                            //子配置の数分ループ。
                            //手数計算のために全ての子配置を参照しないといけない。
                            //if(L == 2) cout << "parent hand = " << table.get(id) << " child hand = ";
                            for (int childid = 0; childid < nchild; childid++) {
                                unsigned int value = table.get(array[childid]); //ここからは子の腰背までの手数を参照して先手勝敗を記録していく。
                                //if(L == 2) cout << value << " ";
                                if(value < (1U << table_entry_size)-1){
                                    if (value%2 == 0) { //子が反転盤面（相手始点）で負けが1つでもあるならなら勝ち。
                                        is_win = true;
                                        if(min_win_steps < 0 || (int)value < min_win_steps){min_win_steps = value;}
                                    }
                                    else if(value%2 == 1){ //子が反転盤面（相手始点）で勝ちならmax_lose_stepsを更新。
                                        if(max_lose_steps < 0 || max_lose_steps < (int)value){
                                            max_lose_steps = value;
                                        }
                                    }
                                }
                                else if (value == (1U << table_entry_size)-1) { //子配置が不明であるなら参照する配置は勝敗不明になる可能性がある。
                                    is_unknown = true;
                                }
                            }
                            //if(L == 2) cout << endl;
                        }

                        //先手勝敗フラグに応じて手数を表に登録
                        if (is_win) { //先手勝ちフラグがtrue
                            assert((min_win_steps+1) % 2 == 1 && min_win_steps+1 < (int)((1U << table_entry_size)-1));
                            if (min_win_steps + 1 >= (int)((1U << table_entry_size) - 1)) {
                                cerr << "Error: Game steps exceeded 254! (Attempted to write " << min_win_steps + 1 << ")" << endl;
                                terminate(); // プログラムをエラー終了
                            }
                            if(min_win_steps+1 == ITER)
                            {
                                table.set_update(id, min_win_steps+1);
                                nwin++;
                                nunknown--;
                            }
                        } else if (is_unknown) {
                            //不明で更新すると正確な更新節点数が分からない。ファイル作成時にすべて不明で初期化しているので書き換える必要はない。
                            //table.set_update(id, (1U << table_entry_size)-1);
                        } else {
                            assert((max_lose_steps+1) % 2 == 0 && max_lose_steps+1 < (int)((1U << table_entry_size)-1));
                            if (max_lose_steps + 1 >= (int)((1U << table_entry_size) - 1)) {
                                cerr << "Error: Game steps exceeded 254! (Attempted to write " << max_lose_steps + 1 << ")" << endl;
                                terminate(); // プログラムをエラー終了
                            }
                            if(max_lose_steps+1 == ITER)
                            {
                                table.set_update(id, max_lose_steps+1);
                                nlose++;
                                nunknown--;
                            }
                        }
                    }
                    //可能手無しは0手先手負けとする
                    else{
                        if(ITER == 0)
                        {
                            table.set_update(id, 0);
                            nlose++;
                            nunknown--;
                        }
                    }
                }
                //処理終了後のworkを元に戻す。
                for(size_t i = 0; i < deq_tmp.size(); i++){
                    assert(nstack_work_idle < 2048);
                    stack_work_idle[ nstack_work_idle++ ] = deq_tmp[i];
                }
            }
            //出力数が最後の配置ID+1（IDは0からなので実質、全体の配置数）以上なら全ての盤面解析終了。
            if (lunknown+1 <= count_output) break;
        }
    }

    //OutTable_By_TwoFile out_table(read_file_name_L);
    OutTable_By_FourFile out_table(read_file_name_LBW, read_file_name_LWB);
    unsigned long long int update_counter = 0;
    unsigned int max_moves = 0;
    //Lファイルの管轄（one_leaves_num_L）の中で、変更のあった要素のみをピンポイントで上書き
    for (unsigned long long int i = 0; i < one_leaves_num_LBW + one_leaves_num_LWB; i++) {
        unsigned int get_moves = table.get(i);
        if (table.is_changed(i)) { 
            out_table.Write_Elem_At(i, get_moves);
            update_counter++;
        }
        //ファイルを見に行くわけではないので初期配置勝敗は常に見ていく。
        //ただし、表のIDをそのまま入れるとオーバーフローが起きてしまうのでLBW側だけで呼ぶ。
        if(i < one_leaves_num_LBW)
        {
            Posi initPosi(i, zdd);
            if(initPosi.Is_Init_Pos()){
                cout << "Init id = " << i << " Init Position Number of Steps = " << get_moves << endl;
            }
        }
        //最大手数を記録
        if(get_moves != 255 && max_moves < get_moves){
            max_moves = get_moves;
        }
    }
    cout << "max moves = " << max_moves << endl;
    cout << "File overwrite completed. Total " << update_counter << " elements updated." << endl;

    unique_lock<mutex> lck(mtx); //ロック
    flag_worker_quit = true; //仕事が終わったことを表すフラグ
    cv_worker.notify_all(); //仕事が全部終了したのでworkerを全て起こす。起きたときに仕事終了フラグを確認して止まる。
}

//bossから与えられたIDの配置の子配置のIDを全て列挙する役割。合法手は配列として返される。
static void worker() noexcept {
    Work *w; //処理するworkの代入先
    while (true) { //仕事を全て終えたフラグが立つまで繰り返す
        unique_lock<mutex> lck(mtx); //ロック
        cv_worker.wait(lck, [&](){ if (0 < deq_input.size()) return true;
                                    return flag_worker_quit; }); //入力キューに仕事がある or (表に記入すべきものがあり、表がkeepされていない) or 仕事が全て終わっている になるまで待機。
        if (flag_worker_quit) break;//仕事が全て完了したならループ終了
        assert(0 < deq_input.size()); //Sleep終了条件から必ず仕事があるはず
        w = deq_input.back(); //入力キューの末尾1つのworkを参照しwに代入
        deq_input.pop_back(); //入力キューの末尾を削除
        lck.unlock();//ロック解除
        
        unsigned long long int table_id = w->get_id();
        bool is_ref_LBW = table_id < one_leaves_num_LBW; // LBWを解析対象にしているか、LWBを対象にしているか。
        unsigned long long int zdd_id = is_ref_LBW ? table_id : table_id - one_leaves_num_LBW; // 盤面フェンス数L
        Posi posi(zdd_id, zdd); //workに与えられた配置のIDから配置を作る

        if(posi.Can_Black_Win()){ //与えられた配置が先手1手勝ち（ゴールラインに到達できる手が存在する配置）であるならば
            unsigned long long int black_win_id = ULLONG_MAX-1; //勝ち確定を表す特殊IDを定義
            w->set_array_id(1, &black_win_id); //可能手の結果は勝ち確定IDだけを含んだ配列
        }
        else{ //先手1手勝ち不可能
            unsigned int actions[3000]; //子配置に遷移させる可能手。行動を整数1つで表現する。int型。
                                        //要素と行動の対応。N=BOARD_SIDE_LENGTH*BOARD_SIDE_LENGTH、M=MAX_FENCE_COUNTとする。
                                        //0~N*N-1はコマをどこに動かしたか。
                                        //手持ちフェンス0~M-1番全てにおいて以下のように値を割り振る。
                                        //N*N+2*m*(N-1)*(N-1)~N*N+(2*m+1)*(N-1)*(N-1)-1はm番目フェンスを縦向きにどこに置いたか。
                                        //N*N+(2*m+1)*(N-1)*(N-1)~N*N+(2*m+2)*(N-1)*(N-1)-1はm番目フェンス横向きにどこに置いたか。
            int nchild = posi.compute_actions(actions, is_ref_LBW ? B > 0 : W > 0); // posiの可能手列挙。actionsに可能手をすべて記録し可能手数を返す。
            if(nchild > 0) { // もし可能手があれば
                unsigned long long int child_array_id[nchild];
                //cout << "table id = " << table_id << " zdd id = " << zdd_id << endl; 
                //posi.View_Position();
                for(int childid = 0; childid < nchild; childid++) { //可能手の数だけループ
                    posi.make_action(actions[childid]); //配置に可能手を適用
                    //cout << "action id = " << actions[childid] << endl;
                    //posi.View_Position();
                    //手持ち反転してもファイルには手持ち情報がないので盤面だけ反転したIDに書き込んでしまう。
                    child_array_id[childid] = posi.compute_id_flip(zdd); //可能手適用後反転することで扱う配置は全て先手から見たものとできる。
                    // ZDDではIDが盤面フェンス数L、L+1の順で盤面が格納されている。
                    // 表ではL2つ、L+1が2つ順に割り当てられる。LBW、LWB、Lp1WBm1、Lp1BWm1の順番。
                    // LBWの解析にはLWBとLp1WBm1が必要、LWBの解析にはLBWとLp1BWm1が必要。
                    // Posiが返すのはZDDのIDなのでオフセットを使って参照位置を表のIDに調整しないといけない。
                    if(table_id < one_leaves_num_LBW){
                        if(actions[childid] < N*N){
                            child_array_id[childid] += one_leaves_num_LBW;
                        }
                        else{
                            child_array_id[childid] += (one_leaves_num_LWB + one_leaves_num_LBW - one_leaves_num_LBW);
                        }
                    }
                    else{
                        if(actions[childid] < N*N){
                            child_array_id[childid] += 0;
                        }
                        else{
                            child_array_id[childid] += (one_leaves_num_LBW + one_leaves_num_LWB + one_leaves_num_Lp1WBm1 - one_leaves_num_LBW);
                        }
                    }
                    posi.unmake_action(actions[childid]); //次の可能手検査のために配置を元に戻す
                }
                w->set_array_id(nchild, child_array_id);
            } else if (nchild == 0) { //何もできない配置。
                unsigned long long int child_array_id[3000];
                w->set_array_id(0, child_array_id); //可能手数0なら可能手数だけ記録。なお最初の後退解析で先手の可能手0の配置を負けにしている。
            } else {
                assert(nchild == -1); //勝敗が付いた終端配置がZDDの配置に含まれるならこれは残す。今回は決着1歩手前までしか含まないので使わない。
                unsigned long long int child_array_id[3000];
                w->set_array_id(-1, child_array_id);
            }    
        }

        w->set_path_length(length); //経路長セット
        lck.lock();//ロック
        deq_output.push_front(w); //boss向けの出力キューに扱った仕事を入れる
        lck.unlock();//ロック解除
        cv_boss.notify_one();//仕事終了をbossに告知してスレッドを起こす
    }
}

//実行時 N L B M Lファイル名 L+1ファイル名 の順で引数定義
//実行する場合ファイル名はquoridorNnMmBbWw_gamevalue.bin
int main(int argc, char *argv[]) {
    L = atoi(argv[2]);
    B = atoi(argv[3]);
    W = atoi(argv[4]);
    ITER = atoi(argv[5]);
    assert(atoi(argv[1]) == N && atoi(argv[2]) == L && atoi(argv[3]) == B && atoi(argv[4]) == W && atoi(argv[5]) == ITER);

    //最初にZDDを構築。
    zdd = ZDD(L);
    one_leaves_num = zdd.One_Leaves_Num();
    one_leaves_num_L = zdd.One_Leaves_Num_L();
    one_leaves_num_Lp1 = zdd.One_Leaves_Num_Lp1();

    one_leaves_num_LBW = one_leaves_num_L;
    one_leaves_num_LWB = one_leaves_num_L;
    one_leaves_num_Lp1WBm1 = B > 0 ? one_leaves_num_Lp1 : 0;
    one_leaves_num_Lp1BWm1 = W > 0 ? one_leaves_num_Lp1 : 0;

    table_size64 = (one_leaves_num_LBW + one_leaves_num_LWB + one_leaves_num_Lp1WBm1 + one_leaves_num_Lp1BWm1 + 64ULL/table_entry_size - 1) / (64ULL/table_entry_size);
    lunknown = one_leaves_num_LBW + one_leaves_num_LWB - 1ULL; //今回勝敗手数を記録するのは盤面フェンス数Lの配置に対してだけ。
    //zdd.View_Root_Info();
    
    //後退解析開始
    //thread th_boss([=]{boss(argv[5], argv[6], argv[7], argv[8]);});
    thread th_boss([=]{boss(argv[6], argv[7], argv[8], argv[9]);});
    thread th_worker[nworker];
    for(int workerid = 0; workerid < nworker; workerid++){
        th_worker[workerid] = thread([&]{worker();});
    }
    th_boss.join();
    for(int workerid = 0; workerid < nworker; workerid++){
        th_worker[workerid].join();
    }

    //結果出力
    cout << "after new nwin  =  " << nwin << endl; //新しい勝ち配置数。
    cout << "after new nlose  =  " << nlose << endl; // 新しい負け配置数。
    cout << "after leave nunknown  =  " << nunknown << endl; //残り不明配置数。
    cout << nwin + nlose << endl; //新たにゲーム値がついた配置
    cout << nunknown << endl; //ゲーム値がついていない配置
    return 0;
}
