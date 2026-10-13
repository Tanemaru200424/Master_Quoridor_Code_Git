#!/bin/bash
set -e

# コンパイル処理（N, L, B, W はすべて実行時引数のため、組合せが変わっても再コンパイル不要）
echo "--- コンパイル開始 ---"
g++ -g -O0 -std=c++11 ../src/reachability/main_quoridor_reachable_NLBW.cpp ../src/common/zdd_quoridor_board_only.cpp ../src/common/posi_quoridor_board_only.cpp -o quridor_reachable_NLBW_obj -pthread

if [ $? -ne 0 ]; then
    echo "--- コンパイル失敗 ---"
    exit 1
fi
echo "--- コンパイル成功 ---"

N=3
MAX_L=2   # = M。ルール上使えるフェンス総数。
HALF=$((MAX_L / 2))  # 片方のプレイヤーが持てるフェンス数の上限 (M/2)

EXECUTABLE="./quridor_reachable_NLBW_obj"

# =========================================================
# 解析ファイルを初期化するか
#
# true  : 全ての(L,B,W)組合せの解析対象ファイルを削除して、最初から解析する
# false : 既存の解析ファイルをそのまま使用して続きから解析する
#
# 到達可能解析はL=0からL=MaxLへ前向きに伝播する設計のため
# (Lが小さい組合せの解析結果が、Lが大きい組合せのファイルに
#  あらかじめ書き込まれている)、後退解析のように組合せごとに
# 個別削除すると、先行する組合せが書き込んだ正当なデータまで
# 失われてしまう。そのため、初期化は全体の実行開始前に
# 1回だけ、該当する全ファイルをまとめて削除する。
# =========================================================
INITIALIZE=true

if [ "$INITIALIZE" = true ]; then
    echo "--- 解析対象ファイルを初期化します (quoridor_reachable_N${N}*.bin を全て削除) ---"
    rm -f quoridor_reachable_N${N}L*.bin
else
    echo "--- 既存の解析対象ファイルを使用します ---"
fi

# ---------------------------------------------------------
# (L, B, W) の1組合せについて、収束するまで反復実行する関数。
# 到達可能配置は2bit(不明/先手/後手/両方)しか記録しないため、
# 後退解析のようなITERによる手数ごとの分割は不要。
# 「total new value count = 0」になった時点でこの組合せは収束。
# ---------------------------------------------------------
run_one_combo() {
    local L=$1
    local B=$2
    local W=$3

    echo "=========================================="
    echo "=== L=${L} B=${B} W=${W} (N=${N}, M=${MAX_L}) ==="
    echo "=========================================="

    local NLBW="quoridor_reachable_N${N}L${L}B${B}W${W}.bin"
    local NLp1Bm1W NLp1BWm1

    if [ "$B" -gt 0 ]; then
        NLp1Bm1W="quoridor_reachable_N${N}L$((L+1))B$((B-1))W${W}.bin"
    else
        NLp1Bm1W="NULL"
    fi

    if [ "$W" -gt 0 ]; then
        NLp1BWm1="quoridor_reachable_N${N}L$((L+1))B${B}W$((W-1)).bin"
    else
        NLp1BWm1="NULL"
    fi

    local loop=1

    while true; do
        echo ""
        echo "--- Iteration ${loop} 開始 (L=${L} B=${B} W=${W}) ---"
        echo "実行コマンドライン"
        echo "${EXECUTABLE} ${N} ${L} ${B} ${W} ${NLBW} ${NLp1Bm1W} ${NLp1BWm1}"

        OUTPUT=$("$EXECUTABLE" "$N" "$L" "$B" "$W" "$NLBW" "$NLp1Bm1W" "$NLp1BWm1")

        echo "実行結果"
        echo "$OUTPUT"
        echo "--------------------------"

        LAST_LINE_1=$(echo "$OUTPUT" | tail -n 2 | head -n 1) # 最後から2行目: 新規到達可能数
        LAST_LINE_2=$(echo "$OUTPUT" | tail -n 1)             # 最後の行: 残り不明数

        if [[ "$LAST_LINE_1" == "0" ]]; then
            echo "total new value count = 0"
            break
        else
            loop=$((loop + 1))
        fi
    done

    echo "=== L=${L} B=${B} W=${W} 完了 ==="
}

# ---------------------------------------------------------
# メインループ
#
# 到達可能配置は初期配置(L=0)から前向きに伝播させるため、
# 後退解析(L降順)とは逆に、Lを0からMaxLまで昇順に処理する。
#
# 各 L で、B を 0 から HALF まで動かし、W = MaxL - L - B とする。
# W が [0, HALF] の範囲に収まる組合せだけを実行する。
# ---------------------------------------------------------
for (( L=0; L<=MAX_L; L++ )); do
    for (( B=0; B<=HALF; B++ )); do
        W=$(( MAX_L - L - B ))
        if (( W < 0 || W > HALF )); then
            continue
        fi
        run_one_combo "$L" "$B" "$W"
    done
done

echo ""
echo "全ての(L,B,W)組合せの実行が終了しました。"