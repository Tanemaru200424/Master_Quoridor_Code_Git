// 文字列を元に、現在のルールにおける配置IDを求め、先手勝敗を求める。
// Fen形式の定義
// ・盤面は後手を上、先手を下にする、先手視点から見ているものとする
// ・コマの盤面は左上から右下に向かって、行に沿って0からマス数-1までの番号を割り振る
// ・フェンスの盤面も同様に番号を割り振る
// ・縮小版Quoridorを扱うため、N（コマ盤面の1辺のマス数）、L（盤上フェンス数）B（先手手持ちフェンス数）W（後手手持ちフェンス数）を指定する必要がある
// ・N L B/W B先手コマ位置/W後手コマ位置 フェンス盤面文字列（0無し、1縦、2横）

#include <array>
#include <iostream>
#include <string>
#include <sstream>
#include <cassert>
#include <string>
#include "zdd_quoridor_board_only.hpp"
#include "posi_quoridor_board_only.hpp"

using namespace std;

int L = 0, B = 0, W = 0;
int M = 0; // 与えられた配置情報から最大でいくつのフェンスを使用するルールか求める
const unsigned int gamevalue_table_entry_size = 8U;
unsigned long long int gamevalue_table_size64 = 0ULL; //uint64を8ビットエントリにした時のゲーム値表のサイズ。
const unsigned int reachability_table_entry_size = 2U;
unsigned long long int reachability_table_size64 = 0ULL; //uint64を8ビットエントリにした時のゲーム値表のサイズ。
unsigned long long int table_elem_num = 0ULL; // 配置を追う際は1つのファイルだけでいいのでZDDのL側さえあればいい。
array<int, N*N + (N-1)*(N-1)> hand_board_info;

// 後退解析の結果を参照する
string GameValue_Table_Filename(int L, int B, int W){
    return "quoridor_gamevalue_N" + to_string(N) + "L" + to_string(L) + "B" + to_string(B) + "W" + to_string(W) + ".bin";
}

class InTable_GameValue{
private:
    unsigned char m_buffer;
    unsigned int m_num_keep;
    ifstream m_ifs_LBW;
public:
    //呼び出すコンストラクタは必ずこっち。ZDD構築後に呼び出すこと。
    InTable_GameValue() noexcept 
    : m_buffer(0), m_num_keep(0){
        string read_file_name_LBW = GameValue_Table_Filename(L, B, W);
        
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
                if(header_LBW[0] != gamevalue_table_entry_size){
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
                if(elem_num_check_LBW != table_elem_num) {
                    cerr << "Size Error" << endl;
                    terminate();
                }
            }
        }
    }
    ~InTable_GameValue() noexcept {
        m_ifs_LBW.close();
    }
    unsigned int Order_Read_One_Elem() noexcept { //要素を1つ目のファイルから順に1つずつ取り出していく。引数の順番にファイルを読み込む。
        unsigned int val = 0;
        if(m_num_keep == 0) {//ファイルから読むのは1バイト、使いたいビット数を考慮。
            if(m_ifs_LBW){
                m_ifs_LBW.read((char*)&m_buffer, 1U);
            }
            else{
                m_buffer = (1U << gamevalue_table_entry_size) - 1;
            }
            m_num_keep = 8U/gamevalue_table_entry_size; //Charは8ビットなので1回でいくつ要素を取り出したか計算。
        }
        val = m_buffer & ((1ULL << gamevalue_table_entry_size) - 1); //表のサイズ分要素取り出し
        m_buffer =  m_buffer >> gamevalue_table_entry_size; //エントリサイズ分右にシフト。
        m_num_keep--; //1組値を使ったのでため込んだ分消費。
        return val;
    }     
};
class Table_GameValue{
private:
    uint64_t *m_table; //ゲーム値書き込み表

    //指定したエントリサイズの表に値を入れる。表はULL配列なのでエントリサイズに対応させる。
    //最初に表を作るときに使う。更新フラグは立てない。
    void set_only(unsigned long long int table_id, unsigned int write_element) noexcept {
        unsigned long long int id64 = table_id / (64ULL/gamevalue_table_entry_size); //ULL配列の何番目の要素にアクセスするか
        unsigned long long int id1 = (table_id % (64ULL/gamevalue_table_entry_size)) * gamevalue_table_entry_size; //アクセスした要素の何ビット目から書き込むか
        unsigned long long int mask = ((1ULL << gamevalue_table_entry_size)- 1) << (64ULL-id1-gamevalue_table_entry_size); //書き込みたい部分にマスクを持っていく
        unsigned long long int t = m_table[id64] & ~mask; //マスクをビット反転して書き込みたい要素の部分を全て0にしておく
        m_table[id64] = t | ((unsigned long long int)write_element << (64ULL-id1-gamevalue_table_entry_size)); //要素を指定領域までシフトして入れる。
    }
    
public:
    Table_GameValue() noexcept
    : m_table(new uint64_t[gamevalue_table_size64]){
        InTable_GameValue inTable_GameValue;
        for (unsigned long long int i = 0; i < table_elem_num; i++) {
            unsigned int v = inTable_GameValue.Order_Read_One_Elem();
            set_only(i, v);
        }
    }
    ~Table_GameValue() noexcept {
        delete [] m_table;
    } //メモリ解法

    //指定したエントリサイズの表から値を取り出す。表はULL配列なのでエントリサイズに対応させる。
    unsigned int get(unsigned long long int table_id) const noexcept {
        unsigned long long int id64 = table_id / (64ULL/gamevalue_table_entry_size); //ULL配列の何番目の要素にアクセスするか
        unsigned long long int id1 = (table_id % (64ULL/gamevalue_table_entry_size)) * gamevalue_table_entry_size; //アクセスした要素の何ビット目から読み込むか
        unsigned int v = ((1ULL << gamevalue_table_entry_size) - 1) & (unsigned int)(m_table[id64] >> (64ULL-id1-gamevalue_table_entry_size)); //取り出したい部分を下位エントリサイズビットに入るよう左シフトしマスクして取り出す。
        return v;
    }
};

// 到達可能の結果を参照する
string Reachability_Table_Filename(int L, int B, int W){
    return "quoridor_reachable_N" + to_string(N) + "L" + to_string(L) + "B" + to_string(B) + "W" + to_string(W) + ".bin";
}

class InTable_Reachability{
private:
    unsigned char m_buffer;
    unsigned int m_num_keep;
    ifstream m_ifs_LBW;
public:
    //呼び出すコンストラクタは必ずこっち。ZDD構築後に呼び出すこと。
    InTable_Reachability() noexcept 
    : m_buffer(0), m_num_keep(0){
        string read_file_name_LBW = Reachability_Table_Filename(L, B, W);
        
        //読み込んだファイルが正しいか確かめるための長さ12のバイト列。
        //0要素は表のエントリ数、1要素は盤面の1辺のマス数、2要素は盤面フェンス数、3要素は先手手持ち、4要素は後手手持ち、以降ZDD1葉経路数。
        //LBWファイル読み込み
        {
            unsigned char header_LBW[12];
            size_t elem_num_check_LBW = 0ULL;
            m_ifs_LBW.open(read_file_name_LBW, ifstream::binary);
            if(!m_ifs_LBW){
                cout << read_file_name_LBW << endl;
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
                if(header_LBW[0] != reachability_table_entry_size){
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
                if(elem_num_check_LBW != table_elem_num) {
                    cerr << "Size Error" << endl;
                    terminate();
                }
            }
        }
    }
    ~InTable_Reachability() noexcept {
        m_ifs_LBW.close();
    }
    unsigned int Order_Read_One_Elem() noexcept { //要素を1つ目のファイルから順に1つずつ取り出していく。引数の順番にファイルを読み込む。
        unsigned int val = 0;
        if(m_num_keep == 0) {//ファイルから読むのは1バイト、使いたいビット数を考慮。
            if(m_ifs_LBW){
                m_ifs_LBW.read((char*)&m_buffer, 1U);
            }
            else{
                m_buffer = (1U << reachability_table_entry_size) - 1;
            }
            m_num_keep = 8U/reachability_table_entry_size; //Charは8ビットなので1回でいくつ要素を取り出したか計算。
        }
        val = m_buffer & ((1ULL << reachability_table_entry_size) - 1); //表のサイズ分要素取り出し
        m_buffer =  m_buffer >> reachability_table_entry_size; //エントリサイズ分右にシフト。
        m_num_keep--; //1組値を使ったのでため込んだ分消費。
        return val;
    }     
};
class Table_Reachability{
private:
    uint64_t *m_table; //ゲーム値書き込み表

    //指定したエントリサイズの表に値を入れる。表はULL配列なのでエントリサイズに対応させる。
    //最初に表を作るときに使う。更新フラグは立てない。
    void set_only(unsigned long long int table_id, unsigned int write_element) noexcept {
        unsigned long long int id64 = table_id / (64ULL/reachability_table_entry_size); //ULL配列の何番目の要素にアクセスするか
        unsigned long long int id1 = (table_id % (64ULL/reachability_table_entry_size)) * reachability_table_entry_size; //アクセスした要素の何ビット目から書き込むか
        unsigned long long int mask = ((1ULL << reachability_table_entry_size)- 1) << (64ULL-id1-reachability_table_entry_size); //書き込みたい部分にマスクを持っていく
        unsigned long long int t = m_table[id64] & ~mask; //マスクをビット反転して書き込みたい要素の部分を全て0にしておく
        m_table[id64] = t | ((unsigned long long int)write_element << (64ULL-id1-reachability_table_entry_size)); //要素を指定領域までシフトして入れる。
    }
    
public:
    Table_Reachability() noexcept
    : m_table(new uint64_t[reachability_table_size64]){
        InTable_Reachability inTable_Reachability;
        for (unsigned long long int i = 0; i < table_elem_num; i++) {
            unsigned int v = inTable_Reachability.Order_Read_One_Elem();
            set_only(i, v);
        }
    }
    ~Table_Reachability() noexcept {
        delete [] m_table;
    } //メモリ解法

    //指定したエントリサイズの表から値を取り出す。表はULL配列なのでエントリサイズに対応させる。
    unsigned int get(unsigned long long int table_id) const noexcept {
        unsigned long long int id64 = table_id / (64ULL/reachability_table_entry_size); //ULL配列の何番目の要素にアクセスするか
        unsigned long long int id1 = (table_id % (64ULL/reachability_table_entry_size)) * reachability_table_entry_size; //アクセスした要素の何ビット目から読み込むか
        unsigned int v = ((1ULL << reachability_table_entry_size) - 1) & (unsigned int)(m_table[id64] >> (64ULL-id1-reachability_table_entry_size)); //取り出したい部分を下位エントリサイズビットに入るよう左シフトしマスクして取り出す。
        return v;
    }
};

// LBWに応じてZDDから得られるIDからいくつずらす必要があるか求める。
unsigned long long int Calclate_ID_Offset(){
    unsigned long long int offset = 0;
    for(int l = 0; l <=L; l++){
        ZDD zdd = ZDD(l);
        unsigned long long int zdd_L_size = zdd.One_Leaves_Num_L();

        int max_black_hand = (M-l) > M/2 ? M/2 : M-1;
        for(int b = 0; b <= max_black_hand; b++){
            int w = M - l - b;
            if(w < 0 || w > M/2){
                continue;
            }
            // オフセット可算。属するZDDの手前まで足す。
            if(l == L && b == B && w == W){
                return offset;
            }
            cout << l << " " << b << " " << w << " " << zdd_L_size << endl;
            offset += zdd_L_size;
        }
    }
    return offset;
}

// 実行時引数としてFen形式を1つの文字列として受ける。
// これを分解してルール部分と盤面部分を受け取り、配置文字列に変換、ZDDで検索する。
int main(int argc, char *argv[]){
    stringstream ss(argv[1]); //受け取った文字列を空白で分割。
    std::string board_size_part, board_fence_num_part, hand_fence_part, pawn_pos_part, board_fence_part;
    
    assert(ss >> board_size_part);   // Nが入る
    assert(ss >> board_fence_num_part);   // Lが入る
    assert(ss >> hand_fence_part);   // BとWが入る
    assert(ss >> pawn_pos_part);  // それぞれのコマの位置が入る
    assert(ss >> board_fence_part);  // フェンスの配列が入る

    assert(stoi(board_size_part) == N);

    L = stoi(board_fence_num_part);

    std::stringstream hand_ss(hand_fence_part);
    std::string b_hand, w_hand;
    std::getline(hand_ss, b_hand, '/'); // Bが入る
    std::getline(hand_ss, w_hand, '/'); // Wが入る
    B = stoi(b_hand);
    W = stoi(w_hand);

    cout << N << " " << L << " " << B << " " << W << endl;

    hand_board_info.fill(0); // 盤面情報を全て0で初期化

    std::stringstream pawn_ss(pawn_pos_part);
    std::string b_pawn, w_pawn;
    std::getline(pawn_ss, b_pawn, '/'); // 先手コマ位置が入る
    std::getline(pawn_ss, w_pawn, '/'); // 後手コマ位置が入る
    int b_pawn_pos = stoi(b_pawn); // コマ盤面から見た位置
    int w_pawn_pos = stoi(w_pawn); // コマ盤面から見た位置
    hand_board_info[(b_pawn_pos/N)*(2*N-1) + (b_pawn_pos%N)] = 1; // コマ盤面の位置を全体盤面の位置に変更して配置情報に値を書く
    hand_board_info[(w_pawn_pos/N)*(2*N-1) + (w_pawn_pos%N)] = 2; // コマ盤面の位置を全体盤面の位置に変更して配置情報に値を書く

    int board_fence_count = 0;
    for (size_t i = 0; i < board_fence_part.length(); ++i) {
        char ch = board_fence_part[i];
        int fence_status = ch - '0';
        if(fence_status != 0){board_fence_count++;}
        hand_board_info[(i/(N-1))*(2*N-1) + i%(N-1) + N] = fence_status;
    }
    assert(board_fence_count == L);

    M = L + B + W;

    ZDD zdd = ZDD(L);
    table_elem_num = zdd.One_Leaves_Num_L();
    gamevalue_table_size64 = (table_elem_num + 64ULL/gamevalue_table_entry_size - 1) / (64ULL/gamevalue_table_entry_size);
    reachability_table_size64 = (table_elem_num + 64ULL/reachability_table_entry_size - 1) / (64ULL/reachability_table_entry_size);

    unsigned long long int offset = Calclate_ID_Offset();
    unsigned long long int zdd_ID = zdd.Compute_ID(&hand_board_info);
    unsigned long long int ref_table_ID = offset + zdd_ID;
    cout << "offset = " << offset << endl;
    cout << "ref ZDD ID = " << zdd_ID << endl;
    cout << "ref table ID = " << ref_table_ID << endl;

    Table_GameValue table_gamevalue;
    cout << table_gamevalue.get(zdd_ID) << endl;
    Table_Reachability table_reachability;
    cout << table_reachability.get(zdd_ID) << endl;

    Posi posi;
    posi.make_posi(zdd_ID, zdd);
    posi.View_Position();
    
    return 0;
}