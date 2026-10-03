#include <iostream>
#include <iomanip>
#include <cassert>
#include <algorithm>
#include <random>
#include "zdd_quoridor_board_only.hpp"
#include "posi_quoridor_board_only.hpp"

void Posi::Black_Pawn_Moves(array<int, 8> *black_pawn_moves, const array<int, N*N + (N-1)*(N-1)> *search_position) const noexcept{ //移動位置は盤面全体の位置情報を基にしている。
    (*black_pawn_moves).fill(-1);
    int black_pawn_pos = 0;
    int white_pawn_pos = 0;
    for(int i = 0; i < N*N+(N-1)*(N-1); i++){
        if(i % (2*N-1) >= N){continue;} //コマ参照じゃないなら何もしない。
        if((*search_position)[i] == 1){black_pawn_pos = i;}
        else if((*search_position)[i] == 2){white_pawn_pos = i;}
        else{continue;}
    }
    int black_pawn_line_id = black_pawn_pos / (2*N-1); //先手コマのマスの行番号。
    int black_pawn_column_id = black_pawn_pos % (2*N-1); //先手コマのマスの列番号。

    //先手コマのマスがルール上移動できる全ての盤面位置を求める。
    //1マス隣の上下左右。
    int up_pawn_pos = (black_pawn_line_id-1)*(2*N-1) + black_pawn_column_id; if(black_pawn_line_id-1 < 0){up_pawn_pos = -1;}
    int right_pawn_pos = black_pawn_line_id*(2*N-1) + black_pawn_column_id+1; if(black_pawn_column_id+1 > N-1){right_pawn_pos = -1;}
    int down_pawn_pos = (black_pawn_line_id+1)*(2*N-1) + black_pawn_column_id; if(black_pawn_line_id+1 > N-1){down_pawn_pos = -1;}
    int left_pawn_pos = black_pawn_line_id*(2*N-1) + black_pawn_column_id-1; if(black_pawn_column_id-1 < 0){left_pawn_pos = -1;}
    //2マス隣の上下左右。後手コマを飛び越える。
    int uu_pawn_pos = (black_pawn_line_id-2)*(2*N-1) + black_pawn_column_id; if(black_pawn_line_id-2 < 0){uu_pawn_pos = -1;}
    int rr_pawn_pos = black_pawn_line_id*(2*N-1) + black_pawn_column_id+2; if(black_pawn_column_id+2 > N-1){rr_pawn_pos = -1;}
    int dd_pawn_pos = (black_pawn_line_id+2)*(2*N-1) + black_pawn_column_id; if(black_pawn_line_id+2 > N-1){dd_pawn_pos = -1;}
    int ll_pawn_pos = black_pawn_line_id*(2*N-1) + black_pawn_column_id-2; if(black_pawn_column_id-2 < 0){ll_pawn_pos = -1;}
    //斜め4方向。後手コマの後ろがフェンスなら使える。
    int ru_pawn_pos = (black_pawn_line_id-1)*(2*N-1) + black_pawn_column_id+1; if(black_pawn_line_id-1 < 0 || black_pawn_column_id+1 > N-1){ru_pawn_pos = -1;}
    int rd_pawn_pos = (black_pawn_line_id+1)*(2*N-1) + black_pawn_column_id+1; if(black_pawn_line_id+1 > N-1 || black_pawn_column_id+1 > N-1){rd_pawn_pos = -1;}
    int ld_pawn_pos = (black_pawn_line_id+1)*(2*N-1) + black_pawn_column_id-1; if(black_pawn_line_id+1 > N-1 || black_pawn_column_id-1 < 0){ld_pawn_pos = -1;}
    int lu_pawn_pos = (black_pawn_line_id-1)*(2*N-1) + black_pawn_column_id-1; if(black_pawn_line_id-1 < 0 || black_pawn_column_id-1 < 0){lu_pawn_pos = -1;}

    //先手コマの移動を制限するフェンスの位置を求める。
    //先手コマに接するフェンス位置。
    int ru_fence_pos = (black_pawn_line_id-1)*(2*N-1) + black_pawn_column_id + N; if(black_pawn_line_id-1 < 0 || black_pawn_column_id > N-2){ru_fence_pos = -1;}
    int rd_fence_pos = black_pawn_line_id*(2*N-1) + black_pawn_column_id + N; if(black_pawn_line_id > N-2 || black_pawn_column_id > N-2){rd_fence_pos = -1;}
    int ld_fence_pos = black_pawn_line_id*(2*N-1) + black_pawn_column_id-1 + N; if(black_pawn_line_id > N-2 || black_pawn_column_id < 1){ld_fence_pos = -1;}
    int lu_fence_pos = (black_pawn_line_id-1)*(2*N-1) + black_pawn_column_id-1 + N; if(black_pawn_line_id-1 < 0 || black_pawn_column_id < 1){lu_fence_pos = -1;}
    //飛び越え、斜め移動の判断で確かめる必要があるフェンス位置。
    int ruu_fence_pos = (black_pawn_line_id-2)*(2*N-1) + black_pawn_column_id + N; if(black_pawn_line_id-2 < 0 || black_pawn_column_id > N-2){ruu_fence_pos = -1;}
    int rru_fence_pos = (black_pawn_line_id-1)*(2*N-1) + black_pawn_column_id+1 + N; if(black_pawn_line_id-1 < 0 || black_pawn_column_id+1 > N-2){rru_fence_pos = -1;}
    int rrd_fence_pos = black_pawn_line_id*(2*N-1) + black_pawn_column_id+1 + N; if(black_pawn_line_id > N-2 || black_pawn_column_id+1 > N-2){rrd_fence_pos = -1;}
    int rdd_fence_pos = (black_pawn_line_id+1)*(2*N-1) + black_pawn_column_id + N; if(black_pawn_line_id+1 > N-2 || black_pawn_column_id > N-2){rdd_fence_pos = -1;}
    int ldd_fence_pos = (black_pawn_line_id+1)*(2*N-1) + black_pawn_column_id-1 + N; if(black_pawn_line_id+1 > N-2 || black_pawn_column_id < 1){ldd_fence_pos = -1;}
    int lld_fence_pos = black_pawn_line_id*(2*N-1) + black_pawn_column_id-2 + N; if(black_pawn_line_id > N-2 || black_pawn_column_id-1 < 1){lld_fence_pos = -1;}
    int llu_fence_pos = (black_pawn_line_id-1)*(2*N-1) + black_pawn_column_id-2 + N; if(black_pawn_line_id-1 < 0 || black_pawn_column_id-1 < 1){llu_fence_pos = -1;}
    int luu_fence_pos = (black_pawn_line_id-2)*(2*N-1) + black_pawn_column_id-1 + N; if(black_pawn_line_id-2 < 0 || black_pawn_column_id < 1){luu_fence_pos = -1;}

    //上への移動について。
    if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 2)) && 
       (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 2))){ //上がフェンスで防がれていないなら
        if(white_pawn_pos != up_pawn_pos){ //上隣のマスに後手コマがないなら
            if(up_pawn_pos != -1){ //移動先が盤面オーバーにならないなら上隣に移動可能。
                (*black_pawn_moves)[0] = up_pawn_pos;
            }
        }
        else{ //上隣のマスに後手コマがあるなら
            if(uu_pawn_pos != -1 &&
            (ruu_fence_pos == -1 || (ruu_fence_pos != -1 && (*search_position)[ruu_fence_pos] != 2)) && 
            (luu_fence_pos == -1 || (luu_fence_pos != -1 && (*search_position)[luu_fence_pos] != 2))){ //移動先が盤面オーバーでなく、後手コマの背後に横向きフェンスがないなら
                (*black_pawn_moves)[0] = uu_pawn_pos; //2つ上に移動可能。
            }
            else{ //相手を飛び越すことができないなら
                if(ru_pawn_pos != -1 && 
                (ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 1)) && 
                (ruu_fence_pos == -1 || (ruu_fence_pos != -1 && (*search_position)[ruu_fence_pos] != 1))){ //後手コマの右側がフェンスで防がれていないなら右上に移動可能。
                    (*black_pawn_moves)[1] = ru_pawn_pos;
                }
                if(lu_pawn_pos != -1 &&
                (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 1)) && 
                (luu_fence_pos == -1 || (luu_fence_pos != -1 && (*search_position)[luu_fence_pos] != 1))){ //後手コマの左側がフェンスで防がれていないなら左上に移動可能。
                    (*black_pawn_moves)[7] = lu_pawn_pos;
                }
            }
        }
    }
    //右への移動について。
    if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 1)) && 
       (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 1))){ //右がフェンスで防がれていないなら
        if(white_pawn_pos != right_pawn_pos){ //右隣のマスに後手コマがないなら
            if(right_pawn_pos != -1){ //移動先が盤面オーバーにならないなら右隣に移動可能。
                (*black_pawn_moves)[2] = right_pawn_pos;
            }
        }
        else{ //右隣のマスに後手コマがあるなら
            if(rr_pawn_pos != -1 &&
            (rru_fence_pos == -1 || (rru_fence_pos != -1 && (*search_position)[rru_fence_pos] != 1)) && 
            (rrd_fence_pos == -1 || (rrd_fence_pos != -1 && (*search_position)[rrd_fence_pos] != 1))){ //移動先が盤面オーバーでなく、後手コマの背後に縦向きフェンスがないなら
                (*black_pawn_moves)[2] = rr_pawn_pos; //2つ右に移動可能。
            }
            else{ //相手を飛び越すことができないなら
                if(ru_pawn_pos != -1 && 
                (ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 2)) && 
                (rru_fence_pos == -1 || (rru_fence_pos != -1 && (*search_position)[rru_fence_pos] != 2))){ //後手コマの上側がフェンスで防がれていないなら右上に移動可能。
                    (*black_pawn_moves)[1] = ru_pawn_pos;
                }
                if(rd_pawn_pos != -1 &&
                (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 2)) && 
                (rrd_fence_pos == -1 || (rrd_fence_pos != -1 && (*search_position)[rrd_fence_pos] != 2))){ //後手コマの下側がフェンスで防がれていないなら右下に移動可能。
                    (*black_pawn_moves)[3] = rd_pawn_pos;
                }
            }
        }
    }
    //下への移動について。
    if((rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 2)) && 
       (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 2))){ //下がフェンスで防がれていないなら
        if(white_pawn_pos != down_pawn_pos){ //下隣のマスに後手コマがないなら
            if(down_pawn_pos != -1){ //移動先が盤面オーバーにならないなら下隣に移動可能。
                (*black_pawn_moves)[4] = down_pawn_pos;
            }
        }
        else{ //下隣のマスに後手コマがあるなら
            if(dd_pawn_pos != -1 &&
            (rdd_fence_pos == -1 || (rdd_fence_pos != -1 && (*search_position)[rdd_fence_pos] != 2)) && 
            (ldd_fence_pos == -1 || (ldd_fence_pos != -1 && (*search_position)[ldd_fence_pos] != 2))){ //移動先が盤面オーバーでなく、後手コマの背後に横向きフェンスがないなら
                (*black_pawn_moves)[4] = dd_pawn_pos; //2つ下に移動可能。
            }
            else{ //相手を飛び越すことができないなら
                if(rd_pawn_pos != -1 && 
                (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 1)) && 
                (rdd_fence_pos == -1 || (rdd_fence_pos != -1 && (*search_position)[rdd_fence_pos] != 1))){ //後手コマの右側がフェンスで防がれていないなら右下に移動可能。
                    (*black_pawn_moves)[3] = rd_pawn_pos;
                }
                if(ld_pawn_pos != -1 &&
                (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 1)) && 
                (ldd_fence_pos == -1 || (ldd_fence_pos != -1 && (*search_position)[ldd_fence_pos] != 1))){ //後手コマの左側がフェンスで防がれていないなら左下に移動可能。
                    (*black_pawn_moves)[5] = ld_pawn_pos;
                }
            }
        }
    }
    //左への移動について。
    if((lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 1)) && 
       (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 1))){ //左がフェンスで防がれていないなら
        if(white_pawn_pos != left_pawn_pos){ //左隣のマスに後手コマがないなら
            if(left_pawn_pos != -1){ //移動先が盤面オーバーにならないなら左隣に移動可能。
                (*black_pawn_moves)[6] = left_pawn_pos;
            }
        }
        else{ //左隣のマスに後手コマがあるなら
            if(ll_pawn_pos != -1 &&
            (llu_fence_pos == -1 || (llu_fence_pos != -1 && (*search_position)[llu_fence_pos] != 1)) && 
            (lld_fence_pos == -1 || (lld_fence_pos != -1 && (*search_position)[lld_fence_pos] != 1))){ //移動先が盤面オーバーでなく、後手コマの背後に縦向きフェンスがないなら
                (*black_pawn_moves)[6] = ll_pawn_pos; //2つ左に移動可能。
            }
            else{ //相手を飛び越すことができないなら
                if(lu_pawn_pos != -1 && 
                (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 2)) && 
                (llu_fence_pos == -1 || (llu_fence_pos != -1 && (*search_position)[llu_fence_pos] != 2))){ //後手コマの上側がフェンスで防がれていないなら左上に移動可能。
                    (*black_pawn_moves)[7] = lu_pawn_pos;
                }
                if(ld_pawn_pos != -1 &&
                (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 2)) && 
                (lld_fence_pos == -1 || (lld_fence_pos != -1 && (*search_position)[lld_fence_pos] != 2))){ //後手コマの下側がフェンスで防がれていないなら左下に移動可能。
                    (*black_pawn_moves)[5] = ld_pawn_pos;
                }
            }
        }
    }
    return;
}

void Posi::Pawn_Moves(array<int, 8> *pawn_moves, const array<int, N*N + (N-1)*(N-1)> *search_position, bool turn) const noexcept{ 
    (*pawn_moves).fill(-1);
    int own_pawn_pos = 0; //手番のコマ位置。
    int opp_pawn_pos = 0; //相手のコマ位置。
    for(int i = 0; i < N*N+(N-1)*(N-1); i++){
        if(i % (2*N-1) >= N){continue;} //コマ参照じゃないなら何もしない。
        if((*search_position)[i] == 1){ //先手コマ発見。
            if(turn == 0){own_pawn_pos = i;}
            else if(turn == 1){opp_pawn_pos = i;}
        }
        if((*search_position)[i] == 2){ //後手コマ発見。
            if(turn == 0){opp_pawn_pos = i;}
            else if(turn == 1){own_pawn_pos = i;}
        }
        else{continue;}
    }
    int own_pawn_line_id = own_pawn_pos / (2*N-1); //先手コマのマスの行番号。
    int own_pawn_column_id = own_pawn_pos % (2*N-1); //先手コマのマスの列番号。

    //先手コマのマスがルール上移動できる全ての盤面位置を求める。
    //1マス隣の上下左右。
    int up_pawn_pos = (own_pawn_line_id-1)*(2*N-1) + own_pawn_column_id; if(own_pawn_line_id-1 < 0){up_pawn_pos = -1;}
    int right_pawn_pos = own_pawn_line_id*(2*N-1) + own_pawn_column_id+1; if(own_pawn_column_id+1 > N-1){right_pawn_pos = -1;}
    int down_pawn_pos = (own_pawn_line_id+1)*(2*N-1) + own_pawn_column_id; if(own_pawn_line_id+1 > N-1){down_pawn_pos = -1;}
    int left_pawn_pos = own_pawn_line_id*(2*N-1) + own_pawn_column_id-1; if(own_pawn_column_id-1 < 0){left_pawn_pos = -1;}
    //2マス隣の上下左右。後手コマを飛び越える。
    int uu_pawn_pos = (own_pawn_line_id-2)*(2*N-1) + own_pawn_column_id; if(own_pawn_line_id-2 < 0){uu_pawn_pos = -1;}
    int rr_pawn_pos = own_pawn_line_id*(2*N-1) + own_pawn_column_id+2; if(own_pawn_column_id+2 > N-1){rr_pawn_pos = -1;}
    int dd_pawn_pos = (own_pawn_line_id+2)*(2*N-1) + own_pawn_column_id; if(own_pawn_line_id+2 > N-1){dd_pawn_pos = -1;}
    int ll_pawn_pos = own_pawn_line_id*(2*N-1) + own_pawn_column_id-2; if(own_pawn_column_id-2 < 0){ll_pawn_pos = -1;}
    //斜め4方向。後手コマの後ろがフェンスなら使える。
    int ru_pawn_pos = (own_pawn_line_id-1)*(2*N-1) + own_pawn_column_id+1; if(own_pawn_line_id-1 < 0 || own_pawn_column_id+1 > N-1){ru_pawn_pos = -1;}
    int rd_pawn_pos = (own_pawn_line_id+1)*(2*N-1) + own_pawn_column_id+1; if(own_pawn_line_id+1 > N-1 || own_pawn_column_id+1 > N-1){rd_pawn_pos = -1;}
    int ld_pawn_pos = (own_pawn_line_id+1)*(2*N-1) + own_pawn_column_id-1; if(own_pawn_line_id+1 > N-1 || own_pawn_column_id-1 < 0){ld_pawn_pos = -1;}
    int lu_pawn_pos = (own_pawn_line_id-1)*(2*N-1) + own_pawn_column_id-1; if(own_pawn_line_id-1 < 0 || own_pawn_column_id-1 < 0){lu_pawn_pos = -1;}

    //先手コマの移動を制限するフェンスの位置を求める。
    //先手コマに接するフェンス位置。
    int ru_fence_pos = (own_pawn_line_id-1)*(2*N-1) + own_pawn_column_id + N; if(own_pawn_line_id-1 < 0 || own_pawn_column_id > N-2){ru_fence_pos = -1;}
    int rd_fence_pos = own_pawn_line_id*(2*N-1) + own_pawn_column_id + N; if(own_pawn_line_id > N-2 || own_pawn_column_id > N-2){rd_fence_pos = -1;}
    int ld_fence_pos = own_pawn_line_id*(2*N-1) + own_pawn_column_id-1 + N; if(own_pawn_line_id > N-2 || own_pawn_column_id < 1){ld_fence_pos = -1;}
    int lu_fence_pos = (own_pawn_line_id-1)*(2*N-1) + own_pawn_column_id-1 + N; if(own_pawn_line_id-1 < 0 || own_pawn_column_id < 1){lu_fence_pos = -1;}
    //飛び越え、斜め移動の判断で確かめる必要があるフェンス位置。
    int ruu_fence_pos = (own_pawn_line_id-2)*(2*N-1) + own_pawn_column_id + N; if(own_pawn_line_id-2 < 0 || own_pawn_column_id > N-2){ruu_fence_pos = -1;}
    int rru_fence_pos = (own_pawn_line_id-1)*(2*N-1) + own_pawn_column_id+1 + N; if(own_pawn_line_id-1 < 0 || own_pawn_column_id+1 > N-2){rru_fence_pos = -1;}
    int rrd_fence_pos = own_pawn_line_id*(2*N-1) + own_pawn_column_id+1 + N; if(own_pawn_line_id > N-2 || own_pawn_column_id+1 > N-2){rrd_fence_pos = -1;}
    int rdd_fence_pos = (own_pawn_line_id+1)*(2*N-1) + own_pawn_column_id + N; if(own_pawn_line_id+1 > N-2 || own_pawn_column_id > N-2){rdd_fence_pos = -1;}
    int ldd_fence_pos = (own_pawn_line_id+1)*(2*N-1) + own_pawn_column_id-1 + N; if(own_pawn_line_id+1 > N-2 || own_pawn_column_id < 1){ldd_fence_pos = -1;}
    int lld_fence_pos = own_pawn_line_id*(2*N-1) + own_pawn_column_id-2 + N; if(own_pawn_line_id > N-2 || own_pawn_column_id-1 < 1){lld_fence_pos = -1;}
    int llu_fence_pos = (own_pawn_line_id-1)*(2*N-1) + own_pawn_column_id-2 + N; if(own_pawn_line_id-1 < 0 || own_pawn_column_id-1 < 1){llu_fence_pos = -1;}
    int luu_fence_pos = (own_pawn_line_id-2)*(2*N-1) + own_pawn_column_id-1 + N; if(own_pawn_line_id-2 < 0 || own_pawn_column_id < 1){luu_fence_pos = -1;}

    //上への移動について。
    if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 2)) && 
       (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 2))){ //上がフェンスで防がれていないなら
        if(opp_pawn_pos != up_pawn_pos){ //上隣のマスに後手コマがないなら
            if(up_pawn_pos != -1){ //移動先が盤面オーバーにならないなら上隣に移動可能。
                (*pawn_moves)[0] = up_pawn_pos;
            }
        }
        else{ //上隣のマスに後手コマがあるなら
            if(uu_pawn_pos != -1 &&
            (ruu_fence_pos == -1 || (ruu_fence_pos != -1 && (*search_position)[ruu_fence_pos] != 2)) && 
            (luu_fence_pos == -1 || (luu_fence_pos != -1 && (*search_position)[luu_fence_pos] != 2))){ //移動先が盤面オーバーでなく、後手コマの背後に横向きフェンスがないなら
                (*pawn_moves)[0] = uu_pawn_pos; //2つ上に移動可能。
            }
            else{ //相手を飛び越すことができないなら
                if(ru_pawn_pos != -1 && 
                (ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 1)) && 
                (ruu_fence_pos == -1 || (ruu_fence_pos != -1 && (*search_position)[ruu_fence_pos] != 1))){ //後手コマの右側がフェンスで防がれていないなら右上に移動可能。
                    (*pawn_moves)[1] = ru_pawn_pos;
                }
                if(lu_pawn_pos != -1 &&
                (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 1)) && 
                (luu_fence_pos == -1 || (luu_fence_pos != -1 && (*search_position)[luu_fence_pos] != 1))){ //後手コマの左側がフェンスで防がれていないなら左上に移動可能。
                    (*pawn_moves)[7] = lu_pawn_pos;
                }
            }
        }
    }
    //右への移動について。
    if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 1)) && 
       (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 1))){ //右がフェンスで防がれていないなら
        if(opp_pawn_pos != right_pawn_pos){ //右隣のマスに後手コマがないなら
            if(right_pawn_pos != -1){ //移動先が盤面オーバーにならないなら右隣に移動可能。
                (*pawn_moves)[2] = right_pawn_pos;
            }
        }
        else{ //右隣のマスに後手コマがあるなら
            if(rr_pawn_pos != -1 &&
            (rru_fence_pos == -1 || (rru_fence_pos != -1 && (*search_position)[rru_fence_pos] != 1)) && 
            (rrd_fence_pos == -1 || (rrd_fence_pos != -1 && (*search_position)[rrd_fence_pos] != 1))){ //移動先が盤面オーバーでなく、後手コマの背後に縦向きフェンスがないなら
                (*pawn_moves)[2] = rr_pawn_pos; //2つ右に移動可能。
            }
            else{ //相手を飛び越すことができないなら
                if(ru_pawn_pos != -1 && 
                (ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 2)) && 
                (rru_fence_pos == -1 || (rru_fence_pos != -1 && (*search_position)[rru_fence_pos] != 2))){ //後手コマの上側がフェンスで防がれていないなら右上に移動可能。
                    (*pawn_moves)[1] = ru_pawn_pos;
                }
                if(rd_pawn_pos != -1 &&
                (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 2)) && 
                (rrd_fence_pos == -1 || (rrd_fence_pos != -1 && (*search_position)[rrd_fence_pos] != 2))){ //後手コマの下側がフェンスで防がれていないなら右下に移動可能。
                    (*pawn_moves)[3] = rd_pawn_pos;
                }
            }
        }
    }
    //下への移動について。
    if((rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 2)) && 
       (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 2))){ //下がフェンスで防がれていないなら
        if(opp_pawn_pos != down_pawn_pos){ //下隣のマスに後手コマがないなら
            if(down_pawn_pos != -1){ //移動先が盤面オーバーにならないなら下隣に移動可能。
                (*pawn_moves)[4] = down_pawn_pos;
            }
        }
        else{ //下隣のマスに後手コマがあるなら
            if(dd_pawn_pos != -1 &&
            (rdd_fence_pos == -1 || (rdd_fence_pos != -1 && (*search_position)[rdd_fence_pos] != 2)) && 
            (ldd_fence_pos == -1 || (ldd_fence_pos != -1 && (*search_position)[ldd_fence_pos] != 2))){ //移動先が盤面オーバーでなく、後手コマの背後に横向きフェンスがないなら
                (*pawn_moves)[4] = dd_pawn_pos; //2つ下に移動可能。
            }
            else{ //相手を飛び越すことができないなら
                if(rd_pawn_pos != -1 && 
                (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 1)) && 
                (rdd_fence_pos == -1 || (rdd_fence_pos != -1 && (*search_position)[rdd_fence_pos] != 1))){ //後手コマの右側がフェンスで防がれていないなら右下に移動可能。
                    (*pawn_moves)[3] = rd_pawn_pos;
                }
                if(ld_pawn_pos != -1 &&
                (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 1)) && 
                (ldd_fence_pos == -1 || (ldd_fence_pos != -1 && (*search_position)[ldd_fence_pos] != 1))){ //後手コマの左側がフェンスで防がれていないなら左下に移動可能。
                    (*pawn_moves)[5] = ld_pawn_pos;
                }
            }
        }
    }
    //左への移動について。
    if((lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 1)) && 
       (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 1))){ //左がフェンスで防がれていないなら
        if(opp_pawn_pos != left_pawn_pos){ //左隣のマスに後手コマがないなら
            if(left_pawn_pos != -1){ //移動先が盤面オーバーにならないなら左隣に移動可能。
                (*pawn_moves)[6] = left_pawn_pos;
            }
        }
        else{ //左隣のマスに後手コマがあるなら
            if(ll_pawn_pos != -1 &&
            (llu_fence_pos == -1 || (llu_fence_pos != -1 && (*search_position)[llu_fence_pos] != 1)) && 
            (lld_fence_pos == -1 || (lld_fence_pos != -1 && (*search_position)[lld_fence_pos] != 1))){ //移動先が盤面オーバーでなく、後手コマの背後に縦向きフェンスがないなら
                (*pawn_moves)[6] = ll_pawn_pos; //2つ左に移動可能。
            }
            else{ //相手を飛び越すことができないなら
                if(lu_pawn_pos != -1 && 
                (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 2)) && 
                (llu_fence_pos == -1 || (llu_fence_pos != -1 && (*search_position)[llu_fence_pos] != 2))){ //後手コマの上側がフェンスで防がれていないなら左上に移動可能。
                    (*pawn_moves)[7] = lu_pawn_pos;
                }
                if(ld_pawn_pos != -1 &&
                (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 2)) && 
                (lld_fence_pos == -1 || (lld_fence_pos != -1 && (*search_position)[lld_fence_pos] != 2))){ //後手コマの下側がフェンスで防がれていないなら左下に移動可能。
                    (*pawn_moves)[5] = ld_pawn_pos;
                }
            }
        }
    }
    return;
}

bool Posi::Is_Exist_Black_Goal_Pass(const array<int, N*N + (N-1)*(N-1)> *search_position) const noexcept {
    bool is_exist_black_pass = false; //先手経路あり
    int black_pawn_pos = 0; //参照盤面における先手コマの位置。
    int search_pos = 0; //探索配列における参照位置。
    int up_search_pos = 0;//探索配列における上隣の位置。
    int right_search_pos = 0; //探索配列における右隣の位置。
    int down_search_pos = 0; //探索配列における下隣の位置。
    int left_search_pos = 0; //探索配列における左隣の位置。
    //int ref_pawn_pos = 0; //参照盤面における参照マスの位置
    int ru_fence_pos = 0; //参照盤面における右上のフェンス位置
    int rd_fence_pos = 0; //参照盤面における右下のフェンス位置
    int ld_fence_pos = 0; //参照盤面における左下のフェンス位置
    int lu_fence_pos = 0; //参照盤面における左上のフェンス位置
    array<char, N*N> route_search_array;//コマのマス分のサイズを持つ、経路追跡用配列。
    deque<int> pawn_move_marks; //コマの通った跡を一時保存し、ループで更新していく。

    for(int i = 0; i < N*N+(N-1)*(N-1); i++){
        if(i % (2*N-1) >= N){continue;} //コマ参照じゃないなら何もしない。
        if((*search_position)[i] == 1){black_pawn_pos = i; break;}
        else{continue;}
    }

    //先手プレイヤーがゴール経路を持つか。
    int black_pawn_line_id = black_pawn_pos / (2*N-1); //先手コマのマスの行番号。
    int black_pawn_column_id = black_pawn_pos % (2*N-1); //先手コマのマスの列番号。
    route_search_array.fill(0); 
    for(int i = 0; i < N; i++){route_search_array[i] = 1;} //ゴールラインの位置を1とラベル付け。
    route_search_array[black_pawn_line_id*N + black_pawn_column_id] = 2; //先手コマの位置を2とラベル付け。
    pawn_move_marks.push_back(black_pawn_line_id*N + black_pawn_column_id); //最初の先手コマの位置を入れる。

    for(int i = 1; i < N; i++){ //ゴールライン以外の行のマスにおいて実行する。
        if(is_exist_black_pass){break;} //経路がもうあるならループしなくていい。
        for(int j = 0; j < 2*N; j++){ //各行において実行する。右から。左からの2回。
            int k = j; if(k > N-1){k = 2*N-1 - j;}
            search_pos = N*i + k;
            up_search_pos = N*(i-1) + k; if(i-1 < 0){up_search_pos = -1;}
            right_search_pos = N*i + k+1; if(k+1 > N-1){right_search_pos = -1;}
            down_search_pos = N*(i+1) + k; if(i+1 > N-1){down_search_pos = -1;}
            left_search_pos = N*i + k-1; if(k-1 < 0){left_search_pos = -1;}
            //ref_pawn_pos = (2*N-1)*i + k;
            ru_fence_pos = (2*N-1)*(i-1) + k + N; if(k > N-2){ru_fence_pos = -1;}
            rd_fence_pos = (2*N-1)*i + k + N; if(i > N-2 || k > N-2){rd_fence_pos = -1;}
            ld_fence_pos = (2*N-1)*i + k-1 + N; if(i > N-2 || k-1 < 0){ld_fence_pos = -1;}
            lu_fence_pos = (2*N-1)*(i-1) + k-1 + N; if(k-1 < 0){lu_fence_pos = -1;}
            if(route_search_array[search_pos] == 0){ //探索位置が0なら
                if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 2)) &&
                (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 2))){ //上を確認できるなら
                    if(up_search_pos != -1 && route_search_array[up_search_pos] == 1){route_search_array[search_pos] = 1;} //上が1なら1にする。
                }
                if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 1)) &&
                (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 1))){ //右を確認できるなら
                    if(right_search_pos != -1 && route_search_array[right_search_pos] == 1){route_search_array[search_pos] = 1;}
                }
                if((rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 2)) &&
                (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 2))){ //下を確認できるなら
                    if(down_search_pos != -1 && route_search_array[down_search_pos] == 1){route_search_array[search_pos] = 1;}
                }
                if((lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 1)) &&
                (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 1))){ //左を確認できるなら
                    if(left_search_pos != -1 && route_search_array[left_search_pos] == 1){route_search_array[search_pos] = 1;}
                }
            }
            else if(route_search_array[search_pos] == 1){ //探索位置が1なら
                if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 2)) &&
                (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 2))){ //上を確認できるなら
                    if(up_search_pos != -1 && route_search_array[up_search_pos] == 2){is_exist_black_pass = true; break;} //上が2なら経路あり。
                }
                else if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 1)) &&
                (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 1))){ //右を確認できるなら
                    if(right_search_pos != -1 && route_search_array[right_search_pos] == 2){is_exist_black_pass = true; break;} //右が2なら経路あり。
                }
                else if((rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 2)) &&
                (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 2))){ //下を確認できるなら
                    if(down_search_pos != -1 && route_search_array[down_search_pos] == 2){is_exist_black_pass = true; break;} //下が2なら経路あり。
                }
                else if((lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 1)) &&
                (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 1))){ //左を確認できるなら
                    if(left_search_pos != -1 && route_search_array[left_search_pos] == 2){is_exist_black_pass = true; break;} //左が2なら経路あり。
                }
            }
        }

        //コマの移動推定部分に全て2にする。
        int pawn_move_marks_size = pawn_move_marks.size();
        for(int j = 0; j < pawn_move_marks_size; j++)
        {
            int ref_mark_pos = pawn_move_marks.front(); //先頭の要素取り出す。
            pawn_move_marks.pop_front(); //先頭削除
            int ref_mark_line_id = ref_mark_pos/N;
            int ref_mark_column_id = ref_mark_pos%N;
            up_search_pos = N*(ref_mark_line_id-1) + ref_mark_column_id; if(ref_mark_line_id-1 < 0){up_search_pos = -1;}
            right_search_pos = N*ref_mark_line_id + ref_mark_column_id+1; if(ref_mark_column_id+1 > N-1){right_search_pos = -1;}
            down_search_pos = N*(ref_mark_line_id+1) + ref_mark_column_id; if(ref_mark_line_id+1 > N-1){down_search_pos = -1;}
            left_search_pos = N*ref_mark_line_id + ref_mark_column_id-1; if(ref_mark_column_id-1 < 0){left_search_pos = -1;}
            ru_fence_pos = (2*N-1)*(ref_mark_line_id-1) + ref_mark_column_id + N; if(ref_mark_column_id > N-2){ru_fence_pos = -1;}
            rd_fence_pos = (2*N-1)*ref_mark_line_id + ref_mark_column_id + N; if(ref_mark_line_id > N-2 || ref_mark_column_id > N-2){rd_fence_pos = -1;}
            ld_fence_pos = (2*N-1)*ref_mark_line_id + ref_mark_column_id-1 + N; if(ref_mark_line_id > N-2 || ref_mark_column_id-1 < 0){ld_fence_pos = -1;}
            lu_fence_pos = (2*N-1)*(ref_mark_line_id-1) + ref_mark_column_id-1 + N; if(ref_mark_column_id-1 < 0){lu_fence_pos = -1;}
            //現在2でマークされた位置を中心に上下左右をマーク。
            if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 2)) &&
            (lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 2))){ //上を確認できるなら
                if(up_search_pos != -1){
                    if(route_search_array[up_search_pos] == 0){
                        route_search_array[up_search_pos] = 2;
                        pawn_move_marks.push_back(up_search_pos);
                    }
                    else if(route_search_array[up_search_pos] == 1){is_exist_black_pass = true; break;}
                }
            }
            if((ru_fence_pos == -1 || (ru_fence_pos != -1 && (*search_position)[ru_fence_pos] != 1)) &&
            (rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 1))){ //右を確認できるなら
                if(right_search_pos != -1){
                    if(route_search_array[right_search_pos] == 0){
                        route_search_array[right_search_pos] = 2;
                        pawn_move_marks.push_back(right_search_pos);
                    }
                    else if(route_search_array[right_search_pos] == 1){is_exist_black_pass = true; break;}
                }
            }
            if((rd_fence_pos == -1 || (rd_fence_pos != -1 && (*search_position)[rd_fence_pos] != 2)) &&
            (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 2))){ //下を確認できるなら
                if(down_search_pos != -1){
                    if(route_search_array[down_search_pos] == 0){
                        route_search_array[down_search_pos] = 2;
                        pawn_move_marks.push_back(down_search_pos);
                    }
                    else if(route_search_array[down_search_pos] == 1){is_exist_black_pass = true; break;}
                }
            }
            if((lu_fence_pos == -1 || (lu_fence_pos != -1 && (*search_position)[lu_fence_pos] != 1)) &&
            (ld_fence_pos == -1 || (ld_fence_pos != -1 && (*search_position)[ld_fence_pos] != 1))){ //左を確認できるなら
                if(left_search_pos != -1){
                    if(route_search_array[left_search_pos] == 0){
                        route_search_array[left_search_pos] = 2;
                        pawn_move_marks.push_back(left_search_pos);
                    }
                    else if(route_search_array[left_search_pos] == 1){is_exist_black_pass = true; break;}
                }
            }
        }
    }

    return is_exist_black_pass;
}

void Posi::Black_Fence_Puts(array<unsigned char, (N-1)*(N-1)> *black_fence_puts, bool canPutFence) const noexcept {
    (*black_fence_puts).fill(0); //全てのフェンス位置を参照し、それらのパターンを記録。最初は全て置けない状態にする。
    if(!canPutFence){return;} //先手手持ちが0枚なら置けないフラグになる。
    int ref_line_id = 0; //参照フェンス行
    int ref_column_id = 0; //参照フェンス列
    int ref_position_id = 0; //参照フェンス位置
    int up_position_id = 0; //参照フェンス位置の上
    int right_position_id = 0; //参照フェンス位置の右
    int down_position_id = 0; //参照フェンス位置の下
    int left_position_id = 0; //参照フェンス位置の左
    array<int, N*N + (N-1)*(N-1)> copy_ref_position = ref_position; //フェンスを置くと仮定した時、それにゴール経路があるか調べる。
    bool can_put_vertical = true; bool can_put_horizontal = true; //実際に置いた後ゴール経路が存在しないといけないので、その有無を管理するフラグ。
    for(int i = 0; i < (int)(*black_fence_puts).size(); i++){
        ref_line_id = i/(N-1);
        ref_column_id = i%(N-1);
        ref_position_id = (2*N-1)*ref_line_id + ref_column_id + N;
        up_position_id = (2*N-1)*(ref_line_id-1) + ref_column_id + N; if(ref_line_id-1 < 0){up_position_id = -1;}
        right_position_id = (2*N-1)*ref_line_id + ref_column_id+1 + N; if(ref_column_id+1 > N-2){right_position_id = -1;}
        down_position_id = (2*N-1)*(ref_line_id+1) + ref_column_id + N; if(ref_line_id+1 > N-2){down_position_id = -1;}
        left_position_id = (2*N-1)*ref_line_id + ref_column_id-1 + N; if(ref_column_id-1 < 0){left_position_id = -1;}
        if(ref_position[ref_position_id] != 0){ //参照位置に既にフェンスがあるなら置けない。
            (*black_fence_puts)[i] = 0;
        }
        else{
            if(((up_position_id != -1 && ref_position[up_position_id] == 1) || (down_position_id != -1 && ref_position[down_position_id] == 1)) &&
            ((right_position_id != -1 && ref_position[right_position_id] == 2) || (left_position_id != -1 && ref_position[left_position_id] == 2))){ //上下に縦、左右に横が存在するなら設置不可。
                can_put_vertical = false; 
                can_put_horizontal = false;
            }
            else if(!((up_position_id != -1 && ref_position[up_position_id] == 1) || (down_position_id != -1 && ref_position[down_position_id] == 1)) &&
            ((right_position_id != -1 && ref_position[right_position_id] == 2) || (left_position_id != -1 && ref_position[left_position_id] == 2))){ //上下に縦が存在せず、左右に横が存在するなら縦のみ設置可能。
                copy_ref_position[ref_position_id] = 1;
                can_put_vertical = Is_Exist_Goal_Pass(&copy_ref_position);
                can_put_horizontal = false;
                copy_ref_position[ref_position_id] = 0;
            }
            else if(((up_position_id != -1 && ref_position[up_position_id] == 1) || (down_position_id != -1 && ref_position[down_position_id] == 1)) &&
            !((right_position_id != -1 && ref_position[right_position_id] == 2) || (left_position_id != -1 && ref_position[left_position_id] == 2))){ //上下に縦が存在し、左右に横が存在しないなら横のみ設置可能。
                can_put_vertical = false;
                copy_ref_position[ref_position_id] = 2;
                can_put_horizontal = Is_Exist_Goal_Pass(&copy_ref_position);
                copy_ref_position[ref_position_id] = 0;
            }
            else if(!((up_position_id != -1 && ref_position[up_position_id] == 1) || (down_position_id != -1 && ref_position[down_position_id] == 1)) &&
            !((right_position_id != -1 && ref_position[right_position_id] == 2) || (left_position_id != -1 && ref_position[left_position_id] == 2))){ //上下に縦、左右に横が存在しないならどちらでも設置可能。
                copy_ref_position[ref_position_id] = 1;
                can_put_vertical = Is_Exist_Goal_Pass(&copy_ref_position);
                copy_ref_position[ref_position_id] = 2;
                can_put_horizontal = Is_Exist_Goal_Pass(&copy_ref_position);
                copy_ref_position[ref_position_id] = 0;
            }
            (*black_fence_puts)[i] = 2*can_put_horizontal + can_put_vertical;
        }
    }
    return;
}

int Posi::compute_actions(unsigned int actions_id[3000], bool canPutFence) const noexcept { //配列に可能手IDを入れる。
    int naction = 0; //可能手数。
    array<int, 8> black_pawn_moves; Black_Pawn_Moves(&black_pawn_moves, &ref_position); //先手コマ移動位置
    array<unsigned char, (N-1)*(N-1)> black_fence_puts; Black_Fence_Puts(&black_fence_puts, canPutFence); //先手フェンス配置位置と向き

    //コマの移動を入れる。
    for(int i = 0; i < 8; i++){
        if(black_pawn_moves[i] != -1){
            int black_pawn_pos_id = N*(black_pawn_moves[i]/(2*N-1)) + black_pawn_moves[i]%(2*N-1); //コマの移動配列に格納されているのは盤面全体から見た位置。なのでコマだけで見たN*N盤面に変換する。
            actions_id[naction] = black_pawn_pos_id; //コマの移動位置はそのまま可能手IDになる。
            naction++;
        }
    }

    //どこにどの向きで置いたかを入れる。ゴール経路が存在し、先手手持ちが存在するならフェンスを使った行動が可能。
    if(Is_Exist_Goal_Pass(&ref_position) && canPutFence){
        for(int i = 0; i < (N-1)*(N-1); i++){
            if(black_fence_puts[i] == 0){continue;} //フェンスを置けないなら何もしない。
            if(black_fence_puts[i] == 1 || black_fence_puts[i] == 3){ //フェンスを縦向きで置けるなら可能手IDに変換。
                actions_id[naction] = N*N + 2*i;
                naction++;
            }
            if(black_fence_puts[i] == 2 || black_fence_puts[i] == 3){ //フェンスを横向きで置けるなら可能手IDに変換。
                actions_id[naction] = N*N + 2*i + 1;
                naction++;
            }
        }
    }

    return naction;
}

bool Posi::exist_action(bool canPutFence) const noexcept {
    array<int, 8> black_pawn_moves; Black_Pawn_Moves(&black_pawn_moves, &ref_position); //先手コマ移動位置
    array<unsigned char, (N-1)*(N-1)> black_fence_puts; Black_Fence_Puts(&black_fence_puts, canPutFence); //先手フェンス配置位置と向き

    //コマの移動を入れる。
    for(int i = 0; i < 8; i++){
        if(black_pawn_moves[i] != -1){return true;} //先手コマに移動可能な場所が1つでもあるなら可能手存在。
    }

    //フェンスをどこにどの向きで置いたかを入れる。    
    if(!canPutFence){return false;} //コマの移動可能位置がない時に手持ちが存在しないなら可能手無し。
    for(int i = 0; i < (N-1)*(N-1); i++){ //コマ盤面のどこかに配置できる位置が存在するなら可能手あり。
        if(black_fence_puts[i] == 0){continue;} 
        else if(black_fence_puts[i] == 1 || black_fence_puts[i] == 3){return true;}
        else if(black_fence_puts[i] == 2 || black_fence_puts[i] == 3){return true;}
    }

    //フェンスを持っていてもフェンスが置けないなら可能手無し。
    return false;
}

void Posi::make_action(unsigned int action_id) noexcept {
    if(action_id < N*N){ //action_idがコマを参照している場合。
        /*
        //デバッグ用。配置出力。
        cout << "before make action" << endl;
        View_Position();
        */
        int black_pawn_pos = (2*N-1)*(action_id/N) + action_id%N; //全体盤面から見たコマの移動位置。acton_idはコマ盤面位置なので変換。
        for(int i = 0; i < N*N+(N-1)*(N-1); i++){ //参照配置の盤面を走査し、先手コマの位置を特定。その後移動を反映。
            if(i % (2*N-1) >= N){continue;} //コマ参照じゃないなら何もしない。
            if(ref_position[i] == 1){
                ref_position[i] = 0;
                ref_position[black_pawn_pos] = 1;
                save_black_pawn_pos = i;
                break;
            } 
            else{continue;}
        }
        /*
        //デバッグ用。配置出力。
        cout << "after make action" << endl;
        View_Position();
        */
    }
    else{ //action_idがフェンスを参照している場合。
        /*
        //デバッグ用。配置出力。
        cout << "before make action" << endl;
        View_Position();
        */
        unsigned int fence_put_pos = (action_id - N*N) / 2; //フェンスを置いた位置。
        unsigned int fence_put_direction = (action_id - N*N) % 2; //置いたフェンスの向き。
        if(fence_put_direction == 0){ //縦向き
            ref_position[fence_put_pos/(N-1)*(2*N-1) + fence_put_pos%(N-1)+N] = 1;
        }
        else if(fence_put_direction == 1){ //横向き
            ref_position[fence_put_pos/(N-1)*(2*N-1) + fence_put_pos%(N-1)+N] = 2;
        }
        /*
        //デバッグ用。配置出力。
        cout << "after make action" << endl;
        View_Position();
        */
    }
    return;
}

void Posi::unmake_action(unsigned int action_id) noexcept{
    
    if(action_id < N*N){ //action_idがコマを参照している場合。
        /*
        //デバッグ用。配置出力。
        cout << "before unmake action" << endl;
        View_Position();
        */
        int black_pawn_pos = (2*N-1)*(action_id/N) + action_id%N ; //全体盤面から見たコマの移動位置。acton_idはコマ盤面位置なので変換。
        if(save_black_pawn_pos != -1){
            ref_position[black_pawn_pos] = 0;
            ref_position[save_black_pawn_pos] = 1;
        }
        /*
        //デバッグ用。配置出力。
        cout << "after unmake action" << endl;
        View_Position();
        */
    }
    else{ //action_idがフェンスを参照している場合。
        /*
        //デバッグ用。配置出力。
        cout << "before unmake action" << endl;
        View_Position();
        */
        unsigned int fence_put_pos = (action_id - N*N) / 2; //フェンスを置いた位置。
        ref_position[fence_put_pos/(N-1)*(2*N-1) + fence_put_pos%(N-1)+N] = 0;
        /*
        //デバッグ用。配置出力。
        cout << "after unmake action" << endl;
        View_Position();
        */
    }
    return;
}

unsigned int Posi::Game_Result_For_Black(bool canPutFence) const noexcept{
    //コマを動かした場合
    //先手コマの上3方向いずれかにゴールが存在するなら勝ち。
    array<int, 8> pawn_moves; Black_Pawn_Moves(&pawn_moves, &ref_position); //先手が現在移動できる位置。
    if((pawn_moves[7] > -1 && pawn_moves[7] < N) ||
       (pawn_moves[0] > -1 && pawn_moves[0] < N) ||
       (pawn_moves[1] > -1 && pawn_moves[1] < N)){
        return 1;
    }
    
    //勝ちがないなら子配置を作る。
    array<int, N*N + (N-1)*(N-1)> child_position; child_position.fill(-1); //先手の可能手後の子配置を入れる。
    child_position = ref_position; //子配置を参照配置と同じにする。

    //コマ移動の場合。
    deque<array<int, N*N + (N-1)*(N-1)>> child_queue; child_queue.clear(); //勝敗を調べるため、先手の可能手後の配置を全て入れる。
    int black_pawn_pos = 0;
    for(int i = 0; i < N*N+(N-1)*(N-1); i++){ //先手コマの位置特定
        if(i % (2*N-1) >= N){continue;} //コマ参照じゃないなら何もしない。
        if(ref_position[i] == 1){black_pawn_pos = i; break;}
    }
    for(int i = 0; i < 8; i++){
        if(pawn_moves[i] != -1){
            child_position[pawn_moves[i]] = 1; //移動先を先手コマにする。
            child_position[black_pawn_pos] = 0; //元いた位置は何もない。
            child_queue.push_back(child_position);
            child_position[pawn_moves[i]] = 0;
            child_position[black_pawn_pos] = 1;
        }
    }
    //フェンスを持っていて置く場合
    array<unsigned char, (N-1)*(N-1)> fence_puts; Black_Fence_Puts(&fence_puts, canPutFence); //参照配置において配置可能なフェンス位置と向き。
    if(ref_position[0] > 0){
        child_position = ref_position; //子配置を参照配置と同じにする。

        for(int i = 0; i < (int)fence_puts.size(); i++){ //どこに置くかでループ
            if(fence_puts[i] == 1 || fence_puts[i] == 3){ //縦における場合
                child_position[i/(N-1)*(2*N-1) + i%(N-1)+N] = 1;
                child_queue.push_back(child_position);
                child_position[i/(N-1)*(2*N-1) + i%(N-1)+N] = 0; //元に戻す。
            }
            if(fence_puts[i] == 2 || fence_puts[i] == 3){ //横における場合
                child_position[i/(N-1)*(2*N-1) + i%(N-1)+N] = 2;
                child_queue.push_back(child_position);
                child_position[i/(N-1)*(2*N-1) + i%(N-1)+N] = 0; //元に戻す。
            }
        }
    }

    array<int, N*N + (N-1)*(N-1)> turning_position; //視点反転配置。
    //子配置があれば検査、なければ不明とする。
    int child_queue_size = child_queue.size();
    for(int i = 0; i < child_queue_size; i++){
        child_position = child_queue.front();
        child_queue.pop_front();

        Turn_View(&turning_position, &child_position); //子配置を反転。
        Black_Pawn_Moves(&pawn_moves, &turning_position); //後手がコマを動かす範囲に後手ゴールがあるか調べる。

        //後手コマの上3方向にゴールがないなら1つでも負けていない子配置があるということになる。
        if(!((pawn_moves[7] > -1 && pawn_moves[7] < N) ||
             (pawn_moves[0] > -1 && pawn_moves[0] < N) ||
             (pawn_moves[1] > -1 && pawn_moves[1] < N))){
            return 0;
        }

        if(i == child_queue_size-1){return 2;} //全ての子配置を調べて全て負けなら
    }
    return 0;
}
