#include "zdd_quoridor_board_only.hpp"   //ヘッダファイルを使う
#include <climits> //0葉の最大最小経路長初期化に使う

//節点nからb(0/1)枝に行ったときに、その節点が0葉かどうかの判定
//フェンスが盤面の格子点数分あるときに全て置くことを考える。この時置かない（0割り振り）択は0葉に行かないといけないが、根の削除はできないため経路が1長くなる。
//コマを先に割り振る場合よりも経路が1長くなるが、節点数は少なくなる。経路が1長くなっても探索速度はあまり変わらない。それならメモリ容量を少しでも削減できるフェンスの方がいい。
bool ZDD::IsNextZeroLeaf(const Node* n, int b) const noexcept {
    assert(n); //ノードが入力されている
    assert(b == 0 || b == 1); //0枝か1枝
    int depth = n->depth; //今の深さをノードから特定

    //盤面割り振り。
    int ref_pos = 0;
    int ref_state = 0;
    int line_id = 0;
    int column_id = 0;
    //今参照しているのがフェンス位置である場合。
    if(depth < 3*(N-1)*(N-1)){
        ref_pos = n->depth / 3;
        ref_state = n->depth % 3;
        line_id = ref_pos / (N - 1);
        column_id = ref_pos % (N - 1);
        if(n->fence_board[Fence_Coordinate_to_Pos(line_id, column_id)] != -1 && b == 1){
            return true; //すでにフェンス位置の状態が決められているのに割り振ろうとしている。
        }
        if(ref_state == 2 && n->fence_board[Fence_Coordinate_to_Pos(line_id, column_id)] == -1 && b == 0){
            return true; //そのマス最後の割り振りの時、まだ何も割り振られていないのに割り振らない。
        }
        //最後のフェンス位置において
        if(line_id == N-2 && column_id == N-2) {
            if(n->board_fence_num < n->goal_L && ref_state == 0 && b == 1) {
                return true; //フェンスが足りないのに空白割り当て。
            }
            if(n->board_fence_num < n->goal_L && ref_state == 2 && b == 0) {
                return true; //フェンスが足りないのに置かない。
            }
        }
        int count_to_end = Fence_Coordinate_to_Pos(N-2, N-2) - Fence_Coordinate_to_Pos(line_id, column_id) + 1; //残り何個のフェンス位置があるか。
        if(count_to_end < n->goal_L - (int)n->board_fence_num) {
            return true; //残りのフェンス数が残りのフェンス位置より多い。
        }
        if(n->goal_L - (int)n->board_fence_num == (int)((N-1)*(N-1)) - (int)n->board_fence_num && ref_state == 0 && b == 1){
            return true; //あと置けるフェンス枚数が残りの配置可能位置と等しい時に0（何もなし）を配置。
        }
        if(n->board_fence_num == n->goal_L && ref_state != 0 && b == 1) {
            return true; //手持ちと盤上のフェンスの合計が上限を超過。
        }
        if(line_id > 0){//上にフェンスが存在しうる場合。
            int up_fence_pos = Fence_Coordinate_to_Pos(line_id-1, column_id);
            if(n->fence_board[up_fence_pos] == 1 && ref_state == 1 && b == 1) {
                return true; //上隣が縦の時に縦で置く。
            }
        }
        if(column_id > 0){//左にフェンスが存在しうる場合。
            int left_fence_pos = Fence_Coordinate_to_Pos(line_id, column_id-1);
            if(n->fence_board[left_fence_pos] == 2 && ref_state == 2 && b == 1) {
                return true; //左隣が横の時に横で置く。
            }
        }
    }
    //今参照しているのがコマのマスである場合。
    else{
        ref_pos = (depth - 3*(N-1)*(N-1)) / 3;
        ref_state = (depth - 3*(N-1)*(N-1)) % 3;
        line_id = ref_pos / N;
        column_id = ref_pos % N;
        if(n->pawn_board[Pawn_Coordinate_to_Pos(line_id, column_id)] != -1 && b == 1){
            return true; //すでにマス状態が決められているのに割り振ろうとしている。
        }
        if(ref_state == 2 && n->pawn_board[Pawn_Coordinate_to_Pos(line_id, column_id)] == -1 && b == 0){
            return true; //そのマス最後の割り振りの時、まだ何も割り振られていないのに割り振らない。
        }
        if(ref_state == 1 && n->black_pawn_pos != -1 && b == 1) {
            return true; //先手コマ重複。
        }
        if(ref_state == 2 && n->white_pawn_pos != -1 && b == 1) {
            return true; //後手コマ重複。
        }
        if(line_id == 0 && ref_state == 1 && b == 1) {
            return true; //先手のゴールラインに先手のコマがある。
        }
        if(line_id == N-1 && ref_state == 2 && b == 1) {
            return true; //後手のゴールラインに後手のコマがある。
        }
        //先手プレイヤー割り振りの最後のマスにおいて、未配置なのに配置しない。
        if(line_id == N-1 && column_id == N-1 && ref_state == 1 && n->black_pawn_pos == -1 && b == 0){
            return true;
        }
        //後手プレイヤー割り振りの最後のマスにおいて、未配置なのに配置しない。
        if(line_id == N-2 && column_id == N-1 && ref_state == 2 && n->white_pawn_pos == -1 && b == 0){
            return true;
        }
        //最後のマスにおいてコマがどちらか欠けているのに空マスを割り当て。
        if(line_id == N-1 && column_id == N-1 && ref_state == 0 && (n->black_pawn_pos == -1 || n->white_pawn_pos == -1) && b == 1) {
            return true;
        }
    }
    
    return false;
}

//ZDDを作る関数
array<Node*, 2> ZDD::Construct_ZDD(int L) noexcept {
    deque<Node*> nodes_que[3*(N*N+(N-1)*(N-1))]; //nodes_que[i]:深さiの節点集合。0～3*(N*N+(N-1)*(N-1))-1。
    unsigned long long int node_count = 0;

    //開始点の作成
    //Node *start[HAND_COMBO_NUM];
    for(int i = 0; i < 2; i++){
        Node *p = new Node;
        p->goal_L = L+i; //目指すべき盤上フェンス数は開始点で決まる。
        p->pawn_board.fill(-1); //要素番号が位置、要素が状態。全ての要素は未定義状態の-1。
        p->fence_board.fill(-1); //要素番号が位置、要素が状態。全ての要素は未定義状態の-1。
        p->board_fence_num = 0;
        p->black_pawn_pos = -1;
        p->white_pawn_pos = -1;
        p->depth = 0;
        p->one_leaves_num = 0;
        p->is_visited = true;
        p->lengthmin = 0;
        p->lengthmax = 0;
        p->total_length = 0;
        p->zero_branch = NULL;
        p->one_branch = NULL;
        p->is_redundant = false;
        nodes_que[0].push_back(p);
    }
    Node* zero_leaf = new Node; //0葉
    Node* one_leaf = new Node; //1葉
    Node* n; //処理をする時参照する用
    node_count = 4; //開始点2つ＋1葉1つ＋0葉1つ

    //0葉設定
    zero_leaf->depth = 3*(N*N+(N-1)*(N-1));
    zero_leaf->one_leaves_num = 0;
    zero_leaf->is_visited = true; //DFSの終着点として訪問済みにしておく。
    zero_leaf->lengthmax = INT_MIN;
    zero_leaf->lengthmin = INT_MAX; 
    zero_leaf->total_length = 0;
    zero_leaf->zero_branch = NULL;
    zero_leaf->one_branch = NULL;
    zero_leaf->is_redundant = false;

    //1葉の設定
    one_leaf->depth = 3*(N*N+(N-1)*(N-1));
    one_leaf->one_leaves_num = 1;
    one_leaf->is_visited = true; //DFSの終着点として訪問済みにしておく。
    one_leaf->lengthmax = 0;
    one_leaf->lengthmin = 0; 
    one_leaf->total_length = 0;
    one_leaf->zero_branch = NULL;
    one_leaf->one_branch = NULL;
    one_leaf->is_redundant = false;

    //各階層のUnique Tableを用意 (キーをuint64_tに変更)
    //std::unordered_map<ID, 既存ノードのポインタ>
    std::vector<std::unordered_map<uint64_t, Node*>> unique_tables(3*(N*N+(N-1)*(N-1)));

    //根に1つ入れるのではなく、手持ちの枚数の割り振り方数分入れる？
    for(int i = 0; i < 3*(N*N+(N-1)*(N-1)); i++){ //各深さに対するループ。
        //盤面割り振り。
        for(size_t k = 0; k < nodes_que[i].size(); k++){  //深さdの節点集合に対するループ
            n = nodes_que[i][k];  //要素(節点)を一つ取り出す
            for(int b = 0; b < 2; b++){ //0枝1枝でのループ。
                if(IsNextZeroLeaf(n, b)){
                    if(b == 0){n->zero_branch = zero_leaf;}
                    else{n->one_branch = zero_leaf;}
                }
                else if(i == 3*(N*N+(N-1)*(N-1)) - 1){
                    if(b == 0){n->zero_branch = one_leaf;}
                    else{n->one_branch = one_leaf;}
                }
                else {
                    //IDを用いた効率的な等価節点共有処理
                    //仮の子ノード情報を一時的に作成 (スタック上のローカル変数で十分)
                    //まだnew Nodeはしない
                    Node temp_child = *n; // 親の情報をコピー
                    temp_child.depth = n->depth + 1;
                    temp_child.one_leaves_num = 0;
                    temp_child.lengthmin = 0;
                    temp_child.lengthmax = 0;
                    temp_child.total_length = 0;
                    temp_child.is_visited = false;
                    temp_child.zero_branch = nullptr;
                    temp_child.one_branch = nullptr;
                    temp_child.is_redundant = false;
                    
                    //現在の深さと枝(b)に応じた状態更新をtemp_childに適用
                    Apply_Transition(&temp_child, i, b); 

                    //この状態に対する一意なIDを取得
                    uint64_t child_id = Get_Node_State_ID(&temp_child);

                    //Unique Table から検索
                    auto& table = unique_tables[i + 1];
                    auto it = table.find(child_id);

                    if (it != table.end()) {
                        //既存の等価ノードを繋ぐ
                        if (b == 0) n->zero_branch = it->second;
                        else n->one_branch = it->second;
                    } 
                    else {
                        //テーブルにないので、実際にメモリを確保して登録
                        Node* real_child = new Node(temp_child); //temp_child の内容で実体化
                        node_count++;

                        table[child_id] = real_child;
                        nodes_que[i + 1].push_back(real_child);

                        if (b == 0) n->zero_branch = real_child;
                        else n->one_branch = real_child;
                    }
                }
            }
        }
    }
    
    //冗長節点の削除。根から葉まで繰り替えし見ていき、新しく削除節点が見つからなくなるまで行う。
    bool is_end = false; //繰り返し終了フラグ。
    while(!is_end){
        is_end = true;
        for(int i = 0; i < 3*(N*N+(N-1)*(N-1))-1; i++){ //深さ0から最後の1つ手前まで。
            for(size_t j = 0; j < nodes_que[i].size(); j++){ //その深さの全ての節点に対して
                //今の節点の子節点の1枝が0葉に繋がるならその子節点を削除。
                if(nodes_que[i][j]->zero_branch->one_branch == zero_leaf && !nodes_que[i][j]->is_redundant){
                    is_end = false;
                    nodes_que[i][j]->zero_branch->is_redundant = true;
                    nodes_que[i][j]->zero_branch = nodes_que[i][j]->zero_branch->zero_branch;
                }
                if(nodes_que[i][j]->one_branch->one_branch == zero_leaf && !nodes_que[i][j]->is_redundant){
                    is_end = false;
                    nodes_que[i][j]->one_branch->is_redundant = true;
                    nodes_que[i][j]->one_branch = nodes_que[i][j]->one_branch->zero_branch;
                }
            }
        }
    }

    //dequeの機能をつかって要素削除
    for(int i = 0; i < 3*(N*N+(N-1)*(N-1)); i++){
        for(size_t j = 0; j < nodes_que[i].size(); j++){
            if(nodes_que[i][j]->is_redundant){
                nodes_que[i].erase(nodes_que[i].begin() + j);
                node_count--;
                j--;
            }
        }
    }

    array<Node*, 2> result;
    unsigned long long int node_count_dfs = 2; //葉は訪問済み判定なので深さ優先探索で弾かれる。先に2足しておく。
    for(size_t i = 0; i < nodes_que[0].size(); i++){
        node_count_dfs += DFS(nodes_que[0][i]);
        result[i] = nodes_que[0][i];
    }
    //ZDD構築と並行して行う節点数計算とDFSによる節点数計算が同じか確かめる。
    cout << "node count = " << node_count << endl;
    cout << "node count dfs = " << node_count_dfs << endl;
    return result;
}