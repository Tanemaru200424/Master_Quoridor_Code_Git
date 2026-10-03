#ifndef INCLUDE_ZDD_BOARD_ONLY_H
#define INCLUDE_ZDD_BOARD_ONLY_H

#include <array>
#include <deque>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <exception>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>

//等価節点共有改良で使う。
#include <unordered_map>
#include <vector>

using std::array;
using std::deque;
using std::max;
using std::min;
using std::copy;
using std::cout;
using std::endl;
using std::cerr;
using std::terminate;
using std::string;
using std::stringstream;

//今回のZDDは手持ちを見ない。盤面のアイテム割り振りだけを追跡する。
//手持ち配置の割り振りなどの盤面外情報は使用者側が管理する必要あり。
//NとLの盤面情報を持つ。LとL+1を開始点として持つ。

static constexpr int N = 5; //盤面の1辺のマス数。

//zddの節点
//手持ち情報は省く
struct Node{
    int goal_L; //開始点ID0ならL、1ならL+1。目指すべき盤上フェンス数。

    array<int, N*N> pawn_board; //要素番号が位置、要素が状態（-1未定義、0なし、1先手、2後手）。
    array<int, (N-1)*(N-1)> fence_board; //要素番号が位置、要素が状態（-1未定義、0なし、1縦、2横）。

    unsigned int board_fence_num; //盤面上フェンス数。初期値0。盤面にフェンスが割り振られたら1増やす。
    int black_pawn_pos; //先手コマの位置。初期値-1。割り振られたら位置を代入。
    int white_pawn_pos; //後手コマの位置。初期値-1。割り振られたら位置を代入。

    unsigned int depth; //ノードの深さ。これを基にアイテムを割り振っていく。

    unsigned long long int one_leaves_num; //その節点から到達できる1葉の数。盤面とIDを対応付ける関数で使う。
    bool is_visited; //DFSで使う。falseで未訪問、trueで訪問済み。 
    int lengthmax;  //最大経路長
    int lengthmin;  //最小経路長
    unsigned long long int total_length; //平均経路長ための1葉到達経路の合計経路長。

    Node *zero_branch; //0-枝
    Node *one_branch; //1-枝
    
    bool is_redundant; //冗長節点のフラグ。trueのものが最後に削除される。
};

//ZDDの機能をまとめたクラス
class ZDD {
private:
    Node* m_starts[2]; //各開始点から始まるZDDを格納。
    unsigned long long int array_one_leaves_num[2]; //盤面フェンス数LとL+1の2つの根。

    //盤面全体のコマのマスの座標からマス盤面における要素番号を返す。
    //コマのマスはN×Nで左上から0右下へ～N^2-1までの番号を付ける。
    int Pawn_Coordinate_to_Pos(int line_id, int column_id) const noexcept{ 
        assert(line_id >= 0 && column_id >= 0);
        return column_id + line_id*N;
    }

    //盤面全体のフェンス位置の座標からフェンス位置盤面における要素番号を返す。
    //フェンスの位置は(N-1)×(N-1)で左上から右下へ0～(N-1)^2-1までの番号を付ける。
    int Fence_Coordinate_to_Pos(int line_id, int column_id) const noexcept{ //フェンス位置から番号を返す。
        assert(line_id >= 0 && column_id >= 0);
        return column_id + line_id*(N-1);
    }

    int Compute_Board_Fence_Num(const array<int, N*N + (N-1)*(N-1)> *hand_board_info) const noexcept{
        unsigned int board_fence_num = 0;
        for(unsigned int i = 0; i < (N-1)*(N-1); i++){
            int fence_line = i/(N-1);
            int fence_colum = i%(N-1);
            int fence_id = (2*N-1)*fence_line + fence_colum + N;
            if((*hand_board_info)[fence_id] != 0){board_fence_num++;}
        }
        return board_fence_num;
    }

    array<Node*, 2> Construct_ZDD(int L) noexcept; //ZDDを構築する関数。盤面フェンス数LとL+1の2つを返す。
    bool IsNextZeroLeaf(const Node* n, int b) const noexcept; //今の節点で指定した枝に進んだ場合、それが0葉になるか。

    unsigned int flip(unsigned int depth) const noexcept { //盤面を反転する時の参照する深さ。
        unsigned int flip_depth = 0;
        unsigned int p = 0;
        unsigned int q = 0;
        if(depth < 3*(N-1)*(N-1)){
            //先に盤面フェンスを割り振る。
            p = depth / 3;
            q = depth % 3;
            flip_depth = 3*((N-1)*(N-1)-1 - p) + q;
        }
        else{
            //次に盤面コマを割り振る。
            p = (depth - 3*(N-1)*(N-1)) / 3;
            q = (depth - 3*(N-1)*(N-1)) % 3;
            flip_depth = 3*(N*N +(N-1)*(N-1)-1 - p) + q;
        }
        
        return flip_depth;
    }

    //配置に値を付ける。同じ深さでこの関数を通して同じ値が付いた節点は等価として扱う。
    uint64_t Get_Node_State_ID(const Node* n) const noexcept {
        uint64_t k = 0;
        int shift = 0;

        //フェンス割り振り中に必要な等価情報を与える。最後のフェンス等価は盤面フェンス全て割り振った次の深さで行われる。
        if(n->depth < 3*(N-1)*(N-1) + 1){
            //あと何枚盤上フェンスを追加できるか (5bit)
            k |= (uint64_t)((n->goal_L - n->board_fence_num) & 0x1F);
            shift += 5;
            
            //最後に置いたフェンスの場所を参照する。
            int ref_pos = n->depth / 3;
            if(ref_pos >= (N-1)*(N-1)){ref_pos = (N-1)*(N-1)-1;}
            int f_line = ref_pos / (N - 1);
            int f_col = ref_pos % (N - 1);
            int fence_id = Fence_Coordinate_to_Pos(f_line, f_col);

            //参照位置が定義済み（-1以外が入っている）かどうかの不一致を検出するためのフラグ
            //ここが異なれば一致でない。
            bool is_defined = (n->fence_board[fence_id] != -1);
            if (is_defined) k |= (1ULL << shift);
            shift++;

            //どちらも未定義（-1）の場合の1つ前の場所を参照。
            if (!is_defined) {
                if (f_col > 0) f_col--;
                else { f_line -= 1; f_col = N - 2; }

                if (f_line < 0) {
                    k |= (1ULL << shift);
                    return k; //line_id < 0 の等価条件
                }
                shift++;
                fence_id = Fence_Coordinate_to_Pos(f_line, f_col);
            }

            //全配置完了の等価条件
            if (n->board_fence_num == (unsigned int)n->goal_L) {
                //「全配置完了分岐を通った」という証拠を 1bit 残す
                k |= (1ULL << shift);
                return k; // 完了状態として返す。
            }
            shift++; //この条件を通らなかった場合も shift を進めて、ビット位置を固定する

            if (f_line < N - 2 && f_col < N - 2) { //最大行でも最大列でもないなら参照フェンスの状態と1行-1分の前の縦向きのフェンスの位置を調べる。
                k |= (uint64_t)(n->fence_board[fence_id] & 0x03) << shift;
                shift += 2;
                for (int i = 1; (i < N - 1 && i < fence_id + 1); i++) {
                    if (n->fence_board[fence_id - i] == 1) k |= (1ULL << shift);
                    shift++;
                }
            }
            else if (f_line == N - 2 && f_col < N - 2) { //最大行なら参照フェンスが横向きか否かと参照フェンスの1つ前の行かつ先の列のフェンスの縦の位置を調べる。
                //横向き(2)か否か
                if (n->fence_board[fence_id] == 2) k |= (1ULL << shift);
                shift++;
                //右側列の縦状態
                for (int i = 0; i < N - 1 - f_col - 1; i++) {
                    if (n->fence_board[fence_id - f_col - 1 - i] == 1) k |= (1ULL << shift);
                    shift++;
                }
            }
            else if (f_line < N - 2 && f_col == N - 2) { //最大列なら参照フェンスと同じ行にある縦のフェンス位置を調べる。
                //今の行の縦状態
                for (int i = 0; i < N - 1; i++) {
                    if (n->fence_board[fence_id - i] == 1) k |= (1ULL << shift);
                    shift++;
                }
            }
        }
        //コマの等価はコマを置いて1つ先の深さからじゃないとできない。
        else{
            //コマの状態 (3bit) 
            //今の参照位置は既に定義されているか、先手コマ後手コマは定義済みか。それぞれ1bitずつ。
            int ref_pos = (n->depth - 3*(N-1)*(N-1)) / 3;
            int p_line = ref_pos / N;
            int p_col = ref_pos % N;
            int now_pawn_pos = Pawn_Coordinate_to_Pos(p_line, p_col);
            if (n->pawn_board[now_pawn_pos] != -1) k |= (1ULL << shift);
            shift++;
            if (n->black_pawn_pos != -1) k |= (1ULL << shift);
            shift++;
            if (n->white_pawn_pos != -1) k |= (1ULL << shift);
            shift++;
        }
        return k;
    }

    //今の深さと枝を基に子節点の情報更新。
    void Apply_Transition(Node* c, int depth, int b) const noexcept {
        if(depth < 3*(N-1)*(N-1)){ //フェンス位置
            int ref_pos = depth / 3;
            int ref_state = depth % 3;
            int f_pos = Fence_Coordinate_to_Pos(ref_pos / (N - 1), ref_pos % (N - 1));
            if(ref_state == 0 && b == 1) {
                c->fence_board[f_pos] = 0;
            }
            else if(ref_state == 1 && b == 1) {
                c->fence_board[f_pos] = 1;
                c->board_fence_num++;
            }
            else if(ref_state == 2 && b == 1) {
                c->fence_board[f_pos] = 2;
                c->board_fence_num++;
            }
        }
        else{ //コマのマス
            int ref_pos = (depth - 3*(N-1)*(N-1)) / 3;
            int ref_state = (depth - 3*(N-1)*(N-1)) % 3;
            int pos = Pawn_Coordinate_to_Pos(ref_pos / N, ref_pos % N);
            if(ref_state == 0 && b == 1) {
                c->pawn_board[pos] = 0;
            }
            else if(ref_state == 1 && b == 1) {
                c->pawn_board[pos] = 1;
                c->black_pawn_pos = pos;
            }
            else if(ref_state == 2 && b == 1) {
                c->pawn_board[pos] = 2;
                c->white_pawn_pos = pos;
            }
        }
    }

public:
    int L = 4; //盤面フェンス数。後退解析を行う際、盤面フェンス数LとL+1の配置集合において後退解析を行う。
    ZDD(){}
    ZDD(int l) {  //コンストラクタ。全ての開始点でZDD生成。
        L = l;
        unsigned long long int start_one_leaves_num = 0;
        array<Node*,2> starts_array = Construct_ZDD(l); //ここでZDDを構築。

        //ZDD構築を基にメンバ変数更新。
        for(int i = 0; i < 2; i++){
            m_starts[i] = starts_array[i];
            start_one_leaves_num += m_starts[i]->one_leaves_num;
            array_one_leaves_num[i] = start_one_leaves_num;
        }
    }
    virtual ~ZDD() {}   //破棄用デストラクタ
    unsigned long long int One_Leaves_Num() const noexcept {return array_one_leaves_num[1];}
    unsigned long long int One_Leaves_Num_L() const noexcept {return array_one_leaves_num[0];}
    unsigned long long int One_Leaves_Num_Lp1() const noexcept {return array_one_leaves_num[1] - array_one_leaves_num[0];}

    //平均経路長を求めるために根から見た1葉の数と1葉までの経路長を求める関数
    unsigned long long int DFS(Node* n) noexcept {
        unsigned long long int node_count = 1;
        if(!n->zero_branch->is_visited) node_count += DFS(n->zero_branch); //0枝の先が未訪問ならその先でもdfs
        if(!n->one_branch->is_visited) node_count += DFS(n->one_branch); //1枝の先が未訪問ならその先でもdfs
        n->one_leaves_num = n->zero_branch->one_leaves_num + n->one_branch->one_leaves_num; //今の節点から到達可能な1葉の数は0枝と1枝の先の節点から到達可能な1葉の数の和
        n->lengthmax = max(n->zero_branch->lengthmax, n->one_branch->lengthmax) + 1; //0枝と1枝のうち長い方に1足したもの。
        n->lengthmin = min(n->zero_branch->lengthmin, n->one_branch->lengthmin) + 1; //0枝と1枝のうち短い方に1足したもの。
        n->total_length = n->zero_branch->total_length + n->one_branch->total_length + n->one_leaves_num; //0枝と1枝の合計経路長に到達可能な1葉の数を足したもの。
        n->is_visited = true; //今の節点を訪問済みにする
        return node_count;
    }
    
    //受け取ったIDから開始点番号を特定する。元は二分探索。
    //今回は開始点がLとL+1の2つだけなのでxがL側1葉経路数より小さいならLの開始点、そうでないならL+1の開始点番号を返す。
    int Compute_Start_ID(unsigned long long int x) const noexcept {
        if(x < array_one_leaves_num[0]){
            return 0;
        }
        else{
            return 1;
        }
    }

    //与えられた番号xに対応する盤面を表す組合せを作る。組合せは引数の配列上で作られる。
    void Compute_Combo(unsigned long long int x, array<int, N*N + (N-1)*(N-1)> *hand_board_info) const noexcept {
        int start_id = Compute_Start_ID(x);
        const Node *r = m_starts[start_id]; //初期位置はIDに対応する開始点
        //渡された配列を初期化。
        (*hand_board_info).fill(-1);
        
        //開始点のIDに応じてxを調整。
        if(start_id > 0){x -= array_one_leaves_num[start_id-1];}

        while (true) {
            if(r-> depth == 3*(N*N +(N-1)*(N-1)) && r->one_leaves_num == 1) {
                break; // もし１葉なら
            }
            //0枝側の節点から到達可能な1葉の数以上のxを持つなら
            if(r->zero_branch->one_leaves_num <= x) {
                x -= r->zero_branch->one_leaves_num; //0枝側の節点から到達可能な1葉の数分xを減らす
                int ref_line = 0;
                int ref_col = 0;
                int ref_state = 0; //どの置き方を参照にしているか。
                if(r->depth < 3*(N-1)*(N-1)){
                    ref_line = (r->depth / 3) / (N-1);
                    ref_col = (r->depth / 3) % (N-1);
                    ref_state = r->depth % 3;
                    (*hand_board_info)[ref_line*(2*N-1) + ref_col+N] = ref_state;
                }
                else{
                    ref_line = ((r->depth - 3*(N-1)*(N-1)) / 3) / N;
                    ref_col = ((r->depth - 3*(N-1)*(N-1)) / 3) % N;
                    ref_state = (r->depth - 3*(N-1)*(N-1)) % 3;
                    (*hand_board_info)[ref_line*(2*N-1) + ref_col] = ref_state;
                }
                r = r->one_branch; //対象の節点から1枝側の節点へ移動
            }
            //そうでないなら 
            else {
                r = r->zero_branch; //0枝側の節点に移動。0枝に移動するのでrに対応するマス状態は加えない。
            }
        }
    }

    //与えられた盤面配列から盤面のIDを計算し返す。
    unsigned long long int Compute_ID(const array<int, N*N + (N-1)*(N-1)> *hand_board_info) const noexcept {
        unsigned long long int index = 0; //返すIDは0で初期化

        //盤上のフェンスを数える必要がある。
        int board_fence_num = Compute_Board_Fence_Num(hand_board_info);
        unsigned int start_id = board_fence_num > L ? 1 : 0;

        const Node* r = m_starts[start_id]; //初期位置は根
        while(r->depth < 3*(N*N + (N-1)*(N-1))){
            int ref_line = 0;
            int ref_col = 0;
            int ref_pos =0;
            int ref_state = 0; //どの置き方を参照にしているか。
            if(r->depth < 3*(N-1)*(N-1)){
                ref_line = (r->depth / 3) / (N-1);
                ref_col = (r->depth / 3) % (N-1);
                ref_pos = ref_line*(2*N-1) + ref_col+N;
                ref_state = r->depth % 3;
            }
            else{
                ref_line = ((r->depth - 3*(N-1)*(N-1)) / 3) / N;
                ref_col = ((r->depth - 3*(N-1)*(N-1)) / 3) % N;
                ref_pos = ref_line*(2*N-1) + ref_col;
                ref_state = (r->depth - 3*(N-1)*(N-1)) % 3;
            }

            if((*hand_board_info)[ref_pos] == ref_state){
                index += r->zero_branch->one_leaves_num;
                r = r->one_branch;
            }
            else{
                r = r->zero_branch;
            }
        }
        if(start_id > 0){index += array_one_leaves_num[start_id-1];}
        return index;
    }

    //ある配置を作るまでの経路長を求める。
    unsigned int Compute_Pass_Length(const array<int, N*N + (N-1)*(N-1)> *hand_board_info){
        unsigned long long int id = Compute_ID(hand_board_info);
        unsigned int pass_length = 0;

        int start_id = Compute_Start_ID(id);
        const Node *r = m_starts[start_id]; //初期位置はIDに対応する開始点
        
        //開始点のIDに応じてxを調整。
        if(start_id > 0){id -= array_one_leaves_num[start_id-1];}

        //int pl = 0; //経路長デバッグ用
        while (true) {
            if(r-> depth == 3*(N*N +(N-1)*(N-1)) && r->one_leaves_num == 1) {
                break; //もし１葉なら
            }
            //0枝側の節点から到達可能な1葉の数以上のxを持つなら
            if(r->zero_branch->one_leaves_num <= id) {
                id -= r->zero_branch->one_leaves_num; //0枝側の節点から到達可能な1葉の数分xを減らす
                r = r->one_branch; //対象の節点から1枝側の節点へ移動
                cout << "1 ";
                pass_length++;
            }
            //そうでないなら 
            else {
                r = r->zero_branch; //0枝側の節点に移動。0枝に移動するのでrに対応するマス状態は加えない。
                cout << "0 ";
                pass_length++;
            }
        }
        cout << pass_length << endl;
        return pass_length;
    }

    //与えられた盤面配列から反転した盤面のIDを計算し返す。
    unsigned long long int Compute_ID_Flip(const array<int, N*N + (N-1)*(N-1)> *hand_board_info) const noexcept {

        //手持ち反転しても盤面フェンス数は変わらないのでそのまま開始点番号を求める。
        int board_fence_num = Compute_Board_Fence_Num(hand_board_info);
        unsigned int start_id = board_fence_num > L ? 1 : 0;

        const Node* r = m_starts[start_id]; //初期位置は根
        unsigned long long int index = 0; //返すIDは0で初期化
        while(r->depth < 3*(N*N + (N-1)*(N-1))){ //全深さ検査
            int flip_depth = flip(r->depth);
            int ref_line = 0;
            int ref_col = 0;
            int ref_pos =0;
            int ref_state = 0; //どの置き方を参照にしているか。
            if(flip_depth < 3*(N-1)*(N-1)){ //盤面フェンス検査時の深さなら位置を点対称移動するだけでいい。
                ref_line = (flip_depth / 3) / (N-1);
                ref_col = (flip_depth / 3) % (N-1);
                ref_pos = ref_line*(2*N-1) + ref_col+N;
                ref_state = flip_depth % 3;
            }
            else{ //コマ検査時の深さなら位置だけでなくコマの色も反転させないといけない。
                ref_line = ((flip_depth - 3*(N-1)*(N-1)) / 3) / N;
                ref_col = ((flip_depth - 3*(N-1)*(N-1)) / 3) % N;
                ref_pos = ref_line*(2*N-1) + ref_col;
                ref_state = (flip_depth - 3*(N-1)*(N-1)) % 3;

                if(ref_state == 1){ref_state = 2;}
                else if(ref_state == 2){ref_state = 1;}
            }
            //盤面情報が一致するならIDに記録していく。
            if((*hand_board_info)[ref_pos] == ref_state){
                index += r->zero_branch->one_leaves_num;
                r = r->one_branch;
            }
            else{
                r = r->zero_branch;
            }
        }
        if(start_id > 0){index += array_one_leaves_num[start_id-1];}
        return index;
    }

    //文字列からIDにする。文字列は（先手手持ち枚数/後手手持ち枚数 コマの状態の行/フェンスの状態の行を繰り返し）
    unsigned long long int BoardString_to_ID(const string& board_string)const noexcept{
        unsigned long long int index = 0;
        array<int, N*N + (N-1)*(N-1)> hand_board_info; hand_board_info.fill(-1);
        stringstream ss(board_string); //受け取った文字列を分割。ルール部分、盤面部分に分かれる。
        string rule_part, board_part; //分けた文字列を入れる用。

        //ルール読み込み
        assert(ss >> rule_part);
        size_t sep1 = rule_part.find('/');
        assert(sep1 != string::npos);

        int fen_N = stoi(rule_part.substr(0, sep1));
        int fen_L = stoi(rule_part.substr(sep1 + 1));

        assert(N == fen_N && L == fen_L); //ルールを守っているか。

        //盤面読み込み
        assert(ss >> board_part);
        //盤面情報からスラッシュを削除
        //std::remove_if と erase-remove イディオムを使用して、スラッシュ文字を効率的に削除
        board_part.erase(
            remove_if(board_part.begin(), board_part.end(), [](char c){ return c == '/'; }),
            board_part.end()
        );
        for (size_t i = 0; i < board_part.length(); i++) {
            char c = board_part[i];
            // 文字から '0' を引くことで、対応する数値 (int) に変換
            hand_board_info[i] = c - '0';
        }
        index = Compute_ID(&hand_board_info);
        return index;
    }
    
    //開始点情報をまとめて根の情報として標準出力。ZDD内に1つの共通の根は用意していないが、存在するものとして深さを計算している。
    void View_Root_Info()const noexcept{
        int now_lengthmax = m_starts[0]->lengthmax;
        int now_lengthmin = m_starts[0]->lengthmin;
        unsigned long long int now_total_length = m_starts[0]->total_length;
        cout << "L = " << L << " leaves = " << array_one_leaves_num[0] << endl;
        
        if(m_starts[1]->lengthmax > now_lengthmax){now_lengthmax = m_starts[1]->lengthmax;}
        if(m_starts[1]->lengthmin < now_lengthmin){now_lengthmin = m_starts[1]->lengthmin;}
        now_total_length += m_starts[1]->total_length;
        cout << "L = " << L+1 << " leaves = " << array_one_leaves_num[1] - array_one_leaves_num[0] << endl;

        cout << "one leaves num = " << array_one_leaves_num[1] << endl;
        cout << "length max = " << now_lengthmax << endl;
        cout << "length min = " << now_lengthmin << endl;
        cout << "total length = " << now_total_length << endl;
        cout << "length ave = " << (double)now_total_length / (double)array_one_leaves_num[1] << endl;
    }

    //ZDDの情報を全部ファイルにかき出す。
    void Output_Root_Info()const noexcept{

        //ファイル名構築
        string filename = "quoridorN" + std::to_string(N) + "BaseL" + std::to_string(L) + "_root_info.txt";
        // std::ofstream を使用してファイルを開く。
        // ファイルがない場合は作成し、ある場合は内容が破棄（上書き）される。
        std::ofstream ofs(filename);
        // ファイルが正常に開けたか確認
        if (!ofs.is_open()) {
            // ファイルが開けない場合はエラーメッセージを出力
            cerr << "エラー: ファイル " << filename << " を開けませんでした。\n";
            return; // 関数を終了
        }

        int now_lengthmax = m_starts[0]->lengthmax;
        int now_lengthmin = m_starts[0]->lengthmin;
        int now_total_length = m_starts[0]->total_length;
        for(int i = 1; i < 2; i++){
            if(m_starts[i]->lengthmax > now_lengthmax){now_lengthmax = m_starts[i]->lengthmax;}
            if(m_starts[i]->lengthmin < now_lengthmin){now_lengthmin = m_starts[i]->lengthmin;}
            now_total_length += m_starts[i]->total_length;
            cout << array_one_leaves_num[i] << endl;
        }

        cout << "one leaves num = " << array_one_leaves_num[1] << endl;
        cout << "length max = " << now_lengthmax << endl;
        cout << "length min = " << now_lengthmin << endl;
        cout << "total length = " << now_total_length << endl;
        cout << "length ave = " << (double)now_total_length / (double)array_one_leaves_num[1] << endl;

        ofs.close();
    }

    //作られた組合せ全てをfen形式に変換。本来のFen形式とは違うが文字列1行で盤面を表現できる。
    void Output_BoardString()const noexcept{
        //ファイル名構築
        string filename = "quoridorN" + std::to_string(N) + "BaseL" + std::to_string(L) + "_fen.txt";
        // std::ofstream を使用してファイルを開く。
        // ファイルがない場合は作成し、ある場合は内容が破棄（上書き）される。
        std::ofstream ofs(filename);
        // ファイルが正常に開けたか確認
        if (!ofs.is_open()) {
            // ファイルが開けない場合はエラーメッセージを出力
            cerr << "エラー: ファイル " << filename << " を開けませんでした。\n";
            return; // 関数を終了
        }

        //盤面とIDの対応を全部出力
        for(unsigned long long int i = 0; i < array_one_leaves_num[1]; i++){
            array<int, N*N + (N-1)*(N-1)> hand_board_info; hand_board_info.fill(-1);
            Compute_Combo(i, &hand_board_info);
            int l = Compute_Board_Fence_Num(&hand_board_info);
            ofs << N << "/" << l << " ";
            
            for(int j = 0; j < (int)hand_board_info.size(); j++){
                ofs << hand_board_info[j];
                if(((j % (N+N-1) == N-1) || (j % (N+N-1) == 2*N-2)) && (j != N*N + (N-1)*(N-1) - 1)){ofs << "/";}
            }
            if(i < array_one_leaves_num[1]-1){ofs << "\n";}
        }
        ofs.close();
    }
    //全ての盤面情報をファイルにかき出す。
    void Output_All_BoardString_to_ID(){
        string input_filename = "quoridorN" + std::to_string(N) + "BaseL" + std::to_string(L) +  "_fen.txt";
        string output_filename = "quoridorN" + std::to_string(N) + "BaseL" + std::to_string(L) + "_fen_id.txt";
        std::ifstream ifs(input_filename);
        std::ofstream ofs(output_filename);
        if (!ifs.is_open()) {
            std::cerr << "エラー: 入力ファイル " << input_filename << " を開けませんでした。\n";
            return;
        }
        if (!ofs.is_open()) {
            std::cerr << "エラー: 出力ファイル " << output_filename << " を開けませんでした。\n";
            return;
        }

        string fen_line;
        unsigned long long int id_result;
        
        while (getline(ifs, fen_line)) {
            // 読み込んだ行に対してBoardString_to_IDを実行
            id_result = BoardString_to_ID(fen_line);
            // 結果を新しいファイルに出力
            ofs << fen_line << " " << id_result << "\n";
        }
        
        ifs.close();
        ofs.close();
    }
    //フェンス枚数ごとの組合せをファイルにかき出す。
    void Output_Each_Fence_Combo(){
        //ファイル名構築
        string filename = "quoridorN" + std::to_string(N) + "BaseL" + std::to_string(L) + "_each_fence_combo.txt";
        // std::ofstream を使用してファイルを開く。
        // ファイルがない場合は作成し、ある場合は内容が破棄（上書き）される。
        std::ofstream ofs(filename);
        // ファイルが正常に開けたか確認
        if (!ofs.is_open()) {
            // ファイルが開けない場合はエラーメッセージを出力
            cerr << "エラー: ファイル " << filename << " を開けませんでした。\n";
            return; // 関数を終了
        }

        //盤面とIDの対応を全部出力
        for(unsigned long long int i = 0; i < array_one_leaves_num[1]; i++){
            array<int, N*N + (N-1)*(N-1)> hand_board_info; hand_board_info.fill(-1);
            Compute_Combo(i, &hand_board_info);
        }
        ofs.close();
    }
};

#endif