# =============================================================================
# Quoridor(縮小版) 後退解析 / 到達可能配置解析 / 表参照 統合 Makefile
#
#   make help      使い方を表示
#   make           3つの実行ファイルをビルド (bin/ に出力)
#   make run       N, L を指定して 到達可能配置解析 + 後退解析 を全組合せ実行
#   make verify    run を最初からやり直し、結果を bin/ の既存データと比較する
#   make clean     オブジェクトファイル・実行ファイルを削除 (bin/ の結果データは残す)
#
# 例)  make verify N=3 L=4
#      make run N=5 L=6          (参照データの無い大きさなので比較は行われない)
#
# ※ GNU make は "Makefile" という名前(Fは小文字)でないと自動では見つけません。
#   別名で使う場合は  make -f MakeFile ...  と指定してください。
# =============================================================================

SHELL := /bin/bash

# ----------------------------- ユーザー指定パラメータ ------------------------
# N : コマ盤面の1辺のマス数
# L : 盤上に置けるフェンス数の最大値 (= ルール上使えるフェンス総数 M。偶数であること)
N            ?= 3
L            ?= 4

# bin/ に参照データがある大きさ。N <= REF_MAX_N かつ L <= REF_MAX_L のときだけ比較する。
REF_MAX_N    ?= 3
REF_MAX_L    ?= 4

# 後退解析の反復(ITER=0,1,2,...)の終了条件。
#   STOP_RULE=bound (既定): unknown が 0 になる、または「判明済みの最大手数+1」まで反復したら終了。
#                           確実に取りこぼしが無い条件(このコンビネーション内に手数の欠番があっても大丈夫)。
#   STOP_RULE=or          : 従来のシェルスクリプトと同じ「ITER>0 で new が STOP_STREAK 回連続0なら終了」。
#                           欠番があると未確定の局面を残したまま止まるため非推奨(比較用に残してある)。
STOP_RULE    ?= bound
STOP_STREAK  ?= 1
ITER_MAX     ?= 254
REACH_ITER_MAX ?= 1000

# 1: bin/ に比較できる参照データが1つも無くても、警告だけで成功扱いにする(既定は0=失敗)
ALLOW_NOREF  ?= 0

# 1: 実行前に解析対象ファイルを削除して最初から解析する / 0: 既存ファイルの続きから
INITIALIZE   ?= 1

CXX          ?= g++
OPT          ?= -O2

# ----------------------------- ディレクトリ・ファイル -----------------------
COMMON_DIR   := src/common
RETRO_DIR    := src/retrograde-analysis
REACH_DIR    := src/reachability
LOOKUP_DIR   := src/lookup
BIN_DIR      := bin
RESULT_ROOT  := results
RESULT_DIR   := $(RESULT_ROOT)/N$(N)L$(L)

ZDD_HPP      := $(COMMON_DIR)/zdd_quoridor_board_only.hpp
HEADERS      := $(wildcard $(COMMON_DIR)/*.hpp)

COMMON_OBJS  := $(COMMON_DIR)/zdd_quoridor_board_only.o \
                $(COMMON_DIR)/posi_quoridor_board_only.o
RETRO_OBJ    := $(RETRO_DIR)/quoridor_retrograde.o
REACH_OBJ    := $(REACH_DIR)/quoridor_reachable.o
LOOKUP_OBJ   := $(LOOKUP_DIR)/quoridor_lookup.o

RETRO_EXE    := $(BIN_DIR)/quoridor_retrograde_exe
REACH_EXE    := $(BIN_DIR)/quoridor_reachable_exe
LOOKUP_EXE   := $(BIN_DIR)/quoridor_lookup_exe
EXES         := $(RETRO_EXE) $(REACH_EXE) $(LOOKUP_EXE)

# Nはコンパイル時定数なので -DQUORIDOR_N で渡す。Nが変わったらオブジェクトを作り直す。
CPPFLAGS     := -I$(COMMON_DIR) -DQUORIDOR_N=$(N)
CXXFLAGS     := -std=c++11 $(OPT) -include cstdint
LDLIBS       := -pthread
BUILD_STAMP  := .build_config

# 初期配置のFEN(表参照のデモ用)。先手コマ=最下段中央、後手コマ=最上段中央、フェンス無し。
HALF_M       := $(shell echo $$(( $(L) / 2 )))
INIT_FEN     ?= $(N) 0 $(HALF_M)/$(HALF_M) $(shell echo $$(( ($(N)-1)*$(N) + $(N)/2 )))/$(shell echo $$(( $(N)/2 ))) $(shell printf '0%.0s' $$(seq 1 $$(( ($(N)-1)*($(N)-1) ))))

.PHONY: all build retrograde reachable lookup run run-retrograde run-reachable \
        lookup-demo lookup-check verify compare clean distclean help \
        patch-header check-header check-L FORCE

all: build

# ----------------------------- ビルド ---------------------------------------
build: $(EXES)
retrograde: $(RETRO_EXE)
reachable:  $(REACH_EXE)
lookup:     $(LOOKUP_EXE)

# Nの値を記録し、変わったときだけスタンプを更新する(=オブジェクトの作り直しを促す)
FORCE:
$(BUILD_STAMP): FORCE
	@printf 'N=%s\n' '$(N)' | cmp -s - $@ 2>/dev/null || printf 'N=%s\n' '$(N)' > $@

$(COMMON_DIR)/%.o: $(COMMON_DIR)/%.cpp $(HEADERS) $(BUILD_STAMP) | check-header
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(RETRO_OBJ): $(RETRO_DIR)/main_quoridor_retrograde_NLBW.cpp $(HEADERS) $(BUILD_STAMP) | check-header
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(REACH_OBJ): $(REACH_DIR)/main_quoridor_reachable_NLBW.cpp $(HEADERS) $(BUILD_STAMP) | check-header
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(LOOKUP_OBJ): $(LOOKUP_DIR)/main_quoridor_lookup_NLBW.cpp $(HEADERS) $(BUILD_STAMP) | check-header
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(RETRO_EXE): $(RETRO_OBJ) $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDLIBS)

$(REACH_EXE): $(REACH_OBJ) $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDLIBS)

$(LOOKUP_EXE): $(LOOKUP_OBJ) $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDLIBS)

# ヘッダーの N が -DQUORIDOR_N で切り替えられる形になっているかを確認する。
check-header:
	@grep -q 'QUORIDOR_N' $(ZDD_HPP) || { \
	  echo "ERROR: $(ZDD_HPP) の N が固定値のままです。Makefile から N を切り替えるには、"; \
	  echo "       次のコマンドでヘッダーの N の定義を1か所だけ書き換えてください:"; \
	  echo "           make patch-header"; \
	  exit 1; }

# static constexpr int N = <固定値>;  を、-DQUORIDOR_N で上書きできる形に書き換える(何度実行しても同じ結果)。
patch-header:
	@if grep -q 'QUORIDOR_N' $(ZDD_HPP); then \
	  echo "$(ZDD_HPP) は既に対応済みです。"; \
	else \
	  sed -i 's|^static constexpr int N = [0-9]*;.*$$|#ifndef QUORIDOR_N\n#define QUORIDOR_N 5 // 盤面の1辺のマス数の既定値。Makefile の N= で上書きされる。\n#endif\nstatic constexpr int N = QUORIDOR_N; // 盤面の1辺のマス数。|' $(ZDD_HPP); \
	  grep -q 'QUORIDOR_N' $(ZDD_HPP) && echo "$(ZDD_HPP) を書き換えました。" || { echo "書き換えに失敗しました。手作業で修正してください。"; exit 1; }; \
	fi

check-L:
	@if [ $$(( $(L) % 2 )) -ne 0 ] || [ $(L) -lt 0 ]; then \
	  echo "ERROR: L(=$(L)) は0以上の偶数を指定してください(片方のプレイヤーの手持ちは L/2 枚)。"; exit 1; fi

# ----------------------------- 解析の実行 -----------------------------------
# 後退解析: Lを大きい方から0へ(終端側から逆算)。各(L,B,W)で ITER=0,1,2,... を自動でカウントアップ。
define RETRO_SCRIPT
cd "$$RESULT_DIR" || exit 1
mkdir -p logs
HALF=$$(( MAXL / 2 ))
declare -A MAXV   # (L,B,W) -> その表(自分のLファイル)に入っている手数の最大値

# 表ファイル中の最大手数(255=不明を除く)をファイルから直接求める。MAXVに記録が無いときの保険。
file_max() {
  if [ ! -f "$$1" ]; then echo 0; return; fi
  od -An -v -t u1 -j 12 "$$1" | tr -s ' ' '\n' | grep -v '^$$' | grep -vx 255 | sort -n | tail -n 1 | awk '{print ($$1=="")?0:$$1}'
}

for (( l=MAXL; l>=0; l-- )); do
  for (( b=0; b<=HALF; b++ )); do
    w=$$(( MAXL - l - b ))
    if (( w < 0 || w > HALF )); then continue; fi
    # 1回の実行で (L,B,W) と (L,W,B) の両方の表を解くので、B<=W の組合せだけ実行すればよい。
    if (( b > w )); then continue; fi
    LBW="quoridor_gamevalue_N$${N}L$${l}B$${b}W$${w}.bin"
    LWB="quoridor_gamevalue_N$${N}L$${l}B$${w}W$${b}.bin"
    P1=NULL; P2=NULL; K1=""; K2=""
    if (( b > 0 )); then P1="quoridor_gamevalue_N$${N}L$$((l+1))B$${w}W$$((b-1)).bin"; K1="$$((l+1)),$${w},$$((b-1))"; fi
    if (( w > 0 )); then P2="quoridor_gamevalue_N$${N}L$$((l+1))B$${b}W$$((w-1)).bin"; K2="$$((l+1)),$${b},$$((w-1))"; fi
    LOG="logs/retrograde_L$${l}B$${b}W$${w}.log"
    : > "$$LOG"
    if [ "$$INITIALIZE" = 1 ]; then rm -f "$$LBW" "$$LWB"; fi
    echo "[retrograde] N=$$N L=$$l B=$$b W=$$w  (表: $$LBW と $$LWB を同時に解く)"

    # 参照するL+1側の表の最大手数(これは今回の実行では変化しない)
    vref=0
    for k in "$$K1" "$$K2"; do
      if [ -n "$$k" ]; then
        vk="$${MAXV[$$k]:-}"
        if [ -z "$$vk" ]; then
          IFS=, read -r kl kb kw <<< "$$k"
          vk=$$(file_max "quoridor_gamevalue_N$${N}L$${kl}B$${kb}W$${kw}.bin")
        fi
        if (( vk > vref )); then vref=$$vk; fi
      fi
    done

    iter=0; streak=0; vcur=0
    while true; do
      out=$$("$$EXE" "$$N" "$$l" "$$b" "$$w" "$$iter" "$$LBW" "$$LWB" "$$P1" "$$P2") || { echo "ERROR: 後退解析の実行に失敗 (L=$$l B=$$b W=$$w ITER=$$iter)"; exit 1; }
      printf '%s\n' "$$out" >> "$$LOG"
      new=$$(printf '%s\n' "$$out" | tail -n 2 | head -n 1)
      unk=$$(printf '%s\n' "$$out" | tail -n 1)
      own=$$(printf '%s\n' "$$out" | grep 'max moves' | tail -n 1 | awk '{print $$NF}')
      own=$${own:-0}
      vcur=$$own
      echo "    ITER=$$iter new=$$new unknown=$$unk (判明済みの最大手数: 自分=$$own, 参照先=$$vref)"
      if [ "$$unk" = "0" ]; then break; fi
      if [ "$$STOP_RULE" = "or" ]; then
        # 従来のシェルスクリプトと同じ条件(非推奨): ITER>0 で new==0 が続いたら打ち切る。
        if (( iter > 0 )); then
          if [ "$$new" = "0" ]; then
            streak=$$(( streak + 1 ))
            if (( streak >= STOP_STREAK )); then echo "    (STOP_RULE=or により打ち切り)"; break; fi
          else
            streak=0
          fi
        fi
      else
        # 未確定の局面に今後付く値は「判明済みの子の値+1」だけ。判明済みの最大値をVとすると、
        # ITER >= V+1 になれば、それ以上新しい値が付く可能性は無い(残りは決着しない局面)。
        v=$$vcur; if (( vref > v )); then v=$$vref; fi
        if (( iter >= v + 1 )); then echo "    (これ以上値が付く可能性が無いので終了。決着しない局面: $$unk)"; break; fi
      fi
      iter=$$(( iter + 1 ))
      if (( iter > ITER_MAX )); then echo "    (ITER_MAX=$$ITER_MAX に達したので打ち切り)"; break; fi
    done
    MAXV["$$l,$$b,$$w"]=$$vcur
    MAXV["$$l,$$w,$$b"]=$$vcur
  done
done
endef

# 到達可能配置解析: 初期配置(L=0)から前向きに伝播するので、Lを0から昇順に。各(L,B,W)で new==0 になるまで反復。
define REACH_SCRIPT
cd "$$RESULT_DIR" || exit 1
mkdir -p logs
HALF=$$(( MAXL / 2 ))
if [ "$$INITIALIZE" = 1 ]; then rm -f quoridor_reachable_N$${N}L*.bin; fi
for (( l=0; l<=MAXL; l++ )); do
  for (( b=0; b<=HALF; b++ )); do
    w=$$(( MAXL - l - b ))
    if (( w < 0 || w > HALF )); then continue; fi
    F="quoridor_reachable_N$${N}L$${l}B$${b}W$${w}.bin"
    P1=NULL; P2=NULL
    if (( b > 0 )); then P1="quoridor_reachable_N$${N}L$$((l+1))B$$((b-1))W$${w}.bin"; fi
    if (( w > 0 )); then P2="quoridor_reachable_N$${N}L$$((l+1))B$${b}W$$((w-1)).bin"; fi
    LOG="logs/reachable_L$${l}B$${b}W$${w}.log"
    : > "$$LOG"
    echo "[reachable] N=$$N L=$$l B=$$b W=$$w"
    loop=1
    while true; do
      out=$$("$$EXE" "$$N" "$$l" "$$b" "$$w" "$$F" "$$P1" "$$P2") || { echo "ERROR: 到達可能解析の実行に失敗 (L=$$l B=$$b W=$$w loop=$$loop)"; exit 1; }
      printf '%s\n' "$$out" >> "$$LOG"
      new=$$(printf '%s\n' "$$out" | tail -n 2 | head -n 1)
      unk=$$(printf '%s\n' "$$out" | tail -n 1)
      echo "    loop=$$loop new=$$new unknown=$$unk"
      if [ "$$new" = "0" ]; then break; fi
      loop=$$(( loop + 1 ))
      if (( loop > REACH_ITER_MAX )); then echo "    (REACH_ITER_MAX に達したので打ち切り)"; break; fi
    done
  done
done
endef

export RETRO_SCRIPT
export REACH_SCRIPT

$(RESULT_DIR):
	@mkdir -p $@

run-retrograde: check-L $(RETRO_EXE) | $(RESULT_DIR)
	@N=$(N) MAXL=$(L) EXE=$(abspath $(RETRO_EXE)) RESULT_DIR=$(RESULT_DIR) INITIALIZE=$(INITIALIZE) \
	 STOP_RULE=$(STOP_RULE) STOP_STREAK=$(STOP_STREAK) ITER_MAX=$(ITER_MAX) bash -c "$$RETRO_SCRIPT"

run-reachable: check-L $(REACH_EXE) | $(RESULT_DIR)
	@N=$(N) MAXL=$(L) EXE=$(abspath $(REACH_EXE)) RESULT_DIR=$(RESULT_DIR) INITIALIZE=$(INITIALIZE) \
	 REACH_ITER_MAX=$(REACH_ITER_MAX) bash -c "$$REACH_SCRIPT"

# 到達可能・後退解析は別々のファイルに書くので make -j2 run で並列実行してもよい。
run: run-reachable run-retrograde
	@echo "=== N=$(N) L=$(L) の全解析が完了しました。結果: $(RESULT_DIR)/ ==="

# ----------------------------- 表参照 ---------------------------------------
lookup-demo: $(LOOKUP_EXE)
	@test -d $(RESULT_DIR) || { echo "ERROR: $(RESULT_DIR) がありません。先に make run を実行してください。"; exit 1; }
	@echo "FEN = $(INIT_FEN)"
	@cd $(RESULT_DIR) && $(abspath $(LOOKUP_EXE)) "$(INIT_FEN)"

# 後退解析ログ中の「初期配置の手数」と、表参照プログラムが返す値が一致するか確認する。
define LOOKUP_CHECK_SCRIPT
HALF=$$(( MAXL / 2 ))
LOG="$$RESULT_DIR/logs/retrograde_L0B$${HALF}W$${HALF}.log"
if [ ! -f "$$LOG" ]; then echo "(後退解析ログが無いので表参照の整合性確認は省略)"; exit 0; fi
expected=$$(grep "Init Position Number of Steps" "$$LOG" | tail -n 1 | awk '{print $$NF}')
if [ -z "$$expected" ]; then echo "(初期配置の手数がログに無いので表参照の整合性確認は省略)"; exit 0; fi
actual=$$(cd "$$RESULT_DIR" && "$$LOOKUP_EXE" "$$FEN" 2>&1 | awk '/ref table ID/{f=1; next} f && /^[0-9]+$$/{print; exit}')
if [ "$$expected" = "$$actual" ]; then
  echo "[OK] 表参照: 初期配置の手数 $$actual (後退解析ログと一致)"
else
  echo "[NG] 表参照: 初期配置の手数が一致しません (後退解析ログ=$$expected, 表参照=$$actual)"; exit 1
fi
endef
export LOOKUP_CHECK_SCRIPT

lookup-check: $(LOOKUP_EXE)
	@N=$(N) MAXL=$(L) RESULT_DIR=$(RESULT_DIR) LOOKUP_EXE=$(abspath $(LOOKUP_EXE)) FEN="$(INIT_FEN)" bash -c "$$LOOKUP_CHECK_SCRIPT"

# ----------------------------- 参照データとの比較 ---------------------------
define COMPARE_SCRIPT
if (( N > REF_MAX_N || MAXL > REF_MAX_L )); then
  echo "N=$$N L=$$MAXL は参照データの範囲外 (N<=$$REF_MAX_N, L<=$$REF_MAX_L) なので、bin/ との比較は行いません。"
  exit 0
fi
if [ ! -d "$$RESULT_DIR" ]; then echo "ERROR: $$RESULT_DIR がありません。先に make run を実行してください。"; exit 1; fi
shopt -s nullglob
ok=0; bad=0; noref=0
for f in "$$RESULT_DIR"/quoridor_*_N$${N}L*.bin; do
  base=$$(basename "$$f")
  ref="$$BIN_DIR/$$base"
  if [ ! -f "$$ref" ]; then
    echo "  [参照なし] $$base"; noref=$$(( noref + 1 ))
  elif cmp -s "$$f" "$$ref"; then
    echo "  [一致]     $$base"; ok=$$(( ok + 1 ))
  else
    echo "  [不一致]   $$base"; bad=$$(( bad + 1 ))
  fi
done
echo "比較結果: 一致=$$ok 不一致=$$bad 参照なし=$$noref"
if (( bad > 0 )); then echo "NG: bin/ の参照データと異なる結果があります。"; exit 1; fi
if (( ok == 0 )); then
  if [ "$$ALLOW_NOREF" = 1 ]; then echo "警告: 比較できたファイルが1つもありません(bin/ に該当する参照データがありません)。"; exit 0; fi
  echo "NG: 比較できたファイルが1つもありません(bin/ に該当する参照データがありません)。"
  echo "    参照データが無い大きさで警告だけにしたいときは  make verify ALLOW_NOREF=1  と指定してください。"
  exit 1
fi
echo "OK: 比較できた全てのファイルが bin/ の参照データと一致しました。"
endef
export COMPARE_SCRIPT

compare:
	@N=$(N) MAXL=$(L) REF_MAX_N=$(REF_MAX_N) REF_MAX_L=$(REF_MAX_L) ALLOW_NOREF=$(ALLOW_NOREF) RESULT_DIR=$(RESULT_DIR) BIN_DIR=$(BIN_DIR) bash -c "$$COMPARE_SCRIPT"

# ビルド → 最初から全解析 → 表参照の確認 → bin/ との比較 を一度に行う。
verify: check-L
	@rm -rf $(RESULT_DIR)
	@$(MAKE) --no-print-directory run INITIALIZE=1
	@$(MAKE) --no-print-directory lookup-check
	@$(MAKE) --no-print-directory compare

# ----------------------------- 後始末 ---------------------------------------
# オブジェクトファイルと実行ファイルだけを削除する。bin/ の結果データ(参照データ)や results/ は消さない。
clean:
	rm -f $(COMMON_OBJS) $(RETRO_OBJ) $(REACH_OBJ) $(LOOKUP_OBJ) $(EXES) $(BUILD_STAMP)

# clean に加えて、make run / verify が作った results/ も削除する。
distclean: clean
	rm -rf $(RESULT_ROOT)

help:
	@echo "使い方:"
	@echo "  make [build]          retrograde / reachable / lookup の3つをビルドして bin/ に出力"
	@echo "  make retrograde|reachable|lookup   個別にビルド"
	@echo "  make run N=3 L=4      N, L における 到達可能配置解析 + 後退解析 を全(L,B,W)組合せ実行"
	@echo "                        (結果は $(RESULT_ROOT)/N<N>L<L>/ に出力)"
	@echo "  make run-reachable / run-retrograde   片方だけ実行"
	@echo "  make lookup-demo      初期配置を表参照プログラムで引く (FEN=... で任意の配置も可)"
	@echo "  make verify N=3 L=4   最初から全解析し、bin/ の既存データと比較 (参照範囲: N<=$(REF_MAX_N), L<=$(REF_MAX_L))"
	@echo "  make compare          既にある結果を bin/ の既存データと比較するだけ"
	@echo "  make clean            オブジェクトファイル・実行ファイルを削除 (bin/ の結果データは残る)"
	@echo "  make distclean        clean + results/ を削除"
	@echo "  make patch-header     zdd_quoridor_board_only.hpp の N を Makefile から切り替えられる形に書き換える"
	@echo "主な変数: N L REF_MAX_N REF_MAX_L STOP_RULE(bound/or) ITER_MAX INITIALIZE(1/0) OPT CXX"
