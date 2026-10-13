#ifndef INCLUDE_POSI_BOARD_ONLY_H
#define INCLUDE_POSI_BOARD_ONLY_H

#include "zdd_quoridor_board_only.hpp"

//Posiも盤面情報だけ。可能手を調べる時先手手持ちが存在するかどうかだけ知る必要がある。

//実際のボードゲームを想定してコマを操作したりある配置の合法手を求める関数等が入っている
class Posi {
private:
    array<int, N*N + (N-1)*(N-1)> ref_position; //参照する配置。
    int save_black_pawn_pos; //unmakeする時に使う。
    void Black_Pawn_Moves(array<int, 8> *black_pawn_moves, const array<int, N*N + (N-1)*(N-1)> *search_position) const noexcept; //参照配置から先手コマを動かす場合、盤面全体から見たコマの位置を配列にして返す。
    void Pawn_Moves(array<int, 8> *pawn_moves, const array<int, N*N + (N-1)*(N-1)> *search_position, bool turn) const noexcept; //参照配置から手番コマを動かす場合、盤面全体から見たコマの位置を配列にして返す。turnは0で先手、1で後手。
   
    bool Is_Exist_Black_Goal_Pass(const array<int, N*N + (N-1)*(N-1)> *search_position) const noexcept; //代入した配置情報において先手がゴール可能か。
    void Turn_View(array<int, N*N + (N-1)*(N-1)> *turning_position, const array<int, N*N + (N-1)*(N-1)> *origin_position) const noexcept{ //視点を反転した配置を作る。手持ちと盤面を反転。その後コマの先手後手を反転。
        for(int i = 0; i < N*N + (N-1)*(N-1); i++){
            (*turning_position)[i] = (*origin_position)[N*N+(N-1)*(N-1)-1 - i]; //盤面をひっくり返す。
            //コマを参照しているなら先手後手コマ反転。
            if(i % (2*N-1) <= N-1){
                if((*turning_position)[i] == 1){(*turning_position)[i] = 2;}
                else if((*turning_position)[i] == 2){(*turning_position)[i] = 1;}
            }
        }
    }
    bool Is_Exist_Goal_Pass(const array<int, N*N + (N-1)*(N-1)> *search_position) const noexcept { //先手後手どちらともゴール可能か調べる。
        array<int, N*N + (N-1)*(N-1)> turning_position;
        Turn_View(&turning_position, search_position);
        return Is_Exist_Black_Goal_Pass(search_position) && Is_Exist_Black_Goal_Pass(&turning_position);
    }
    
    void Black_Fence_Puts(array<unsigned char, (N-1)*(N-1)> *black_fence_puts, bool canPutFence) const noexcept; //先手が置けるフェンス状況。

public:    
    bool Is_Debug_Goal_Pass(){
        return Is_Exist_Goal_Pass(&ref_position);
    }
    void View_Position() const noexcept {
        //デバッグ用。配置出力。
        cout << "now ref position" << endl;
        for(int i = 0; i < N*N + (N-1)*(N-1); i++){
            cout << ref_position[i] << " ";
            if(i % (2*N-1) == N-1){cout << endl; cout << " ";}
            else if(i % (2*N-1) == 2*N-2){cout << endl;}
        }     
        cout << endl;   
    }
    
    Posi() noexcept { //コンストラクタ
        for(int i = 0; i < (int)ref_position.size(); i++){
            ref_position[i] = 0;
        }
        save_black_pawn_pos = -1;
    }
    
    Posi(unsigned long long int id, const ZDD& zdd) noexcept { //コンストラクタ。IDに対応するZDD1葉配置をref_postionに入れる。
        zdd.Compute_Combo(id, &ref_position);
        save_black_pawn_pos = -1;
        /*
        //デバッグ用。配置出力。
        cout << "construct" << endl;
        cout << "id = " << id << endl;
        cout << Game_Result_For_Black() << endl;
        View_Position();
        */
    }

    void make_posi(unsigned long long int id, const ZDD& zdd) noexcept { //IDに対応するZDD1葉配置をref_postionに入れる。
        zdd.Compute_Combo(id, &ref_position);
        /*
        cout << "make posi" << endl;
        cout << "id = " << id << endl;
        cout << Game_Result_For_Black() << endl;
        View_Position();
        */
    } 
    
    unsigned long long int compute_id(const ZDD& zdd) const noexcept { //自分が担当する配置ZDDの1葉配置のどれにあたるか返す。
        return zdd.Compute_ID(&ref_position);
    }
    unsigned long long int compute_id_flip(const ZDD& zdd) const noexcept { //自分が担当する配置ZDDの1葉配置のどれにあたるか返す。
        return zdd.Compute_ID_Flip(&ref_position);
    } 

    unsigned int compute_naction(bool canPutFence)const noexcept { //今の盤面における先手可能手数を数えるだけ。
        unsigned int naction = 0; //可能手数。
        array<int, 8> black_pawn_moves; Black_Pawn_Moves(&black_pawn_moves, &ref_position); //先手コマ移動位置
        array<unsigned char, (N-1)*(N-1)> black_fence_puts; Black_Fence_Puts(&black_fence_puts, canPutFence); //先手フェンス配置位置と向き

        //コマの移動を入れる。
        for(int i = 0; i < 8; i++){
            if(black_pawn_moves[i] != -1){naction++;}
        }

        //フェンスを持っているなら、どこにどの向きで置けるかを入れる。
        if(canPutFence){
            for(int i = 0; i < (N-1)*(N-1); i++){
                if(black_fence_puts[i] == 0){continue;} //フェンスを置けないなら何もしない。
                if(black_fence_puts[i] == 1 || black_fence_puts[i] == 3){naction++;}
                if(black_fence_puts[i] == 2 || black_fence_puts[i] == 3){naction++;}
            }
        }
        return naction;
    }
    
    int compute_actions(unsigned int actions_id[3000], bool canPutFence) const noexcept; //ref_positionの可能手を配列に入れて、可能手数を返す。配列サイズは8*8*2*10 + 81 = 1361。
    
    void make_action(unsigned int action_id) noexcept; //引数に対応する行動を行いref_positionを変化させる。
    
    bool exist_action(bool canPutFence) const noexcept; //先手の可能手が1つでも存在するならtrueを返して処理を打ち切る。
    
    void unmake_action(unsigned int action_id) noexcept; //引数に対応する行動を行ったと仮定し、ref_positionを行動前に。make_actionを行った後にのみ使える実装になっている。

    unsigned int Game_Result_For_Black(bool canPutFence) const noexcept; //0で勝敗がついていない（不明）、1で勝ち、2で負け。

    bool Can_Black_Win() const noexcept{ //先手コマがこの手番でゴールラインに達しているならtrue、そうでないならfalse。
        //先手コマを動かした場合
        //先手コマの上3方向いずれかにゴールが存在するなら勝ち。
        array<int, 8> pawn_moves; Black_Pawn_Moves(&pawn_moves, &ref_position); //先手が現在移動できる位置。
        if((pawn_moves[7] > -1 && pawn_moves[7] < N) ||
           (pawn_moves[0] > -1 && pawn_moves[0] < N) ||
           (pawn_moves[1] > -1 && pawn_moves[1] < N)){
            return true;
        }
        return false;
    }
    
    bool Is_Black_Goal() const noexcept{ //先手コマが現在ゴールラインに達しているならtrue、そうでないならfalse。
        for(int i = 0; i < N; i++){
            if(ref_position[i] == 1){return true;}
        }
        return false;
    }

    unsigned int Make_Action_Game_Result() const noexcept{ //make_action後、その勝敗を判定する。反復数1の場合のみ使う。
        for(int i = 0; i < N; i++){ //ゴールラインに先手コマがあるなら勝ち。
            if(ref_position[i] == 1){
                
                //デバッグ用。配置出力。
                //cout << "win position" << endl;
                //View_Position();
                
                return 1;
            }
        }

        array<int, N*N + (N-1)*(N-1)> turning_position; //視点反転配置。
        Turn_View(&turning_position, &ref_position); //参照配置の反転を参照。
        array<int, 8> pawn_moves; Black_Pawn_Moves(&pawn_moves, &turning_position); //後手がコマを動かす範囲に後手ゴールがあるか調べる。
        if((pawn_moves[7] > -1 && pawn_moves[7] < N) || (pawn_moves[0] > -1 && pawn_moves[0] < N) || (pawn_moves[1] > -1 && pawn_moves[1] < N)){
            return 2;
        }
        return 0;
    }

    bool Is_Init_Pos() const noexcept{ //参照配置が初期配置か。
        //コマ位置チェック。
        if(ref_position[N/2] != 2 || ref_position[N*N +(N-1)*(N-1) - (N+1)/2] != 1){return false;}
        //フェンス空チェック
        for(int i = 0; i < (N-1)*(N-1); i++){
            if(ref_position[(2*N-1)*(i/(N-1)) + (i%(N-1)) + N] != 0){return false;}
        }
        return true;
    }
};

#endif