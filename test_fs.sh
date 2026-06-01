#!/bin/bash
# test_fs.sh — fs_sim kapsamli test paketi
# Kullanim: ./test_fs.sh [-v|--verbose]

FS="./fs_sim"
PASS=0
FAIL=0
TOTAL=0
VERBOSE=0

for arg in "$@"; do
    case "$arg" in
        -v|--verbose) VERBOSE=1 ;;
        *) printf "Bilinmeyen arguman: %s\nKullanim: %s [-v|--verbose]\n" "$arg" "$0"; exit 1 ;;
    esac
done

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
DIM='\033[2m'
BOLD='\033[1m'
NC='\033[0m'

if [ ! -f "$FS" ]; then
    echo "Hata: '$FS' bulunamadi. Once 'make' calistirin."
    exit 1
fi

# Temp dosyalar: alt kabuktan parent'a veri tasimak icin
_TMPOUT=$(mktemp)
_TMPCMDS=$(mktemp)
_TMPFLAG=$(mktemp)

cleanup() { rm -f "$_TMPOUT" "$_TMPCMDS" "$_TMPFLAG" virtual_disk.bin; }
trap cleanup EXIT

# Her test grubu oncesi temiz disk
fresh() { rm -f virtual_disk.bin; }

# Komutlari fs_sim'e pipe'lar; hem stdout'a hem temp dosyaya yazar
run() {
    printf '%s\n' "$@" > "$_TMPCMDS"
    printf 'new'       > "$_TMPFLAG"
    { printf '%s\n' "$@"; printf 'exit\n'; } | "$FS" 2>&1 | tee "$_TMPOUT"
}

# Verbose: ayni run() icin yalnizca bir kez yazdirir
_verbose_dump() {
    [ "$VERBOSE" -eq 0 ] && return
    [ "$(cat "$_TMPFLAG")" != "new" ] && return
    printf 'shown' > "$_TMPFLAG"
    printf "${DIM}       komutlar:\n"
    while IFS= read -r cmd; do
        [ -n "$cmd" ] && printf "         FS> %s\n" "$cmd"
    done < "$_TMPCMDS"
    printf "       cikti:\n"
    grep -v '^Sanal Dosya\|^Format\|hazir\.' "$_TMPOUT" \
        | grep -v '^$' \
        | grep -v '^FS> $' \
        | sed 's/^/         /'
    printf "${NC}"
}

# "Cikti su pattern'i icermeli"
ok() {
    local name="$1" out="$2" pat="$3"
    TOTAL=$((TOTAL+1))
    if printf '%s' "$out" | grep -q "$pat"; then
        printf "  ${GREEN}PASS${NC}  %s\n" "$name"
        _verbose_dump
        PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${NC}  %s\n" "$name"
        printf "       beklenen pattern : %s\n" "$pat"
        printf "       son 3 satir      : %s\n" \
            "$(printf '%s' "$out" | grep -v '^FS>' | tail -3)"
        _verbose_dump
        FAIL=$((FAIL+1))
    fi
}

# "Cikti su pattern'i icermemeli"
no() {
    local name="$1" out="$2" pat="$3"
    TOTAL=$((TOTAL+1))
    if ! printf '%s' "$out" | grep -q "$pat"; then
        printf "  ${GREEN}PASS${NC}  %s\n" "$name"
        _verbose_dump
        PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${NC}  %s\n" "$name"
        printf "       icermemeli pattern : %s\n" "$pat"
        _verbose_dump
        FAIL=$((FAIL+1))
    fi
}

section() {
    if [ "$VERBOSE" -eq 1 ]; then
        printf "\n${BOLD}${YELLOW}━━━ %s ${NC}${DIM}(verbose)${NC}\n" "$1"
    else
        printf "\n${BOLD}${YELLOW}━━━ %s ${NC}\n" "$1"
    fi
}

# ================================================================
section "CREATE"
# ================================================================

fresh
out=$(run 'create "a.txt"' 'ls')
ok "normal dosya olusturma"              "$out" "a.txt"

fresh
out=$(run 'create "a.txt"' 'create "a.txt"')
ok "ayni isimde dosya → hata"           "$out" "Hata.*zaten var"

fresh
out=$(run 'create "bosluk var.txt"' 'ls')
ok "bosluklu dosya adi destekleniyor"   "$out" "bosluk var.txt"

fresh
out=$(run 'create "a.txt"' 'ls')
ok "yeni dosya ls'de gorunuyor"          "$out" "a.txt"

# ================================================================
section "WRITE / READ"
# ================================================================

fresh
out=$(run 'create "f.txt"' 'write "f.txt" "merhaba"' 'read "f.txt"')
ok "yazilan veri geri okunuyor"         "$out" "merhaba"

fresh
out=$(run 'create "f.txt"' 'write "f.txt" "eski"' 'write "f.txt" "yeni"' 'read "f.txt"')
ok "uzerine yazma: yeni icerik var"     "$out" "yeni"
no "uzerine yazma: eski icerik yok"     "$out" "^eski$"

fresh
out=$(run 'create "f.txt"' 'write "f.txt" "test veri"' 'ls')
ok "write sonrasi ls'de boyut gozukuyor" "$out" "9"

fresh
out=$(run 'write "yok.txt" "x"')
ok "olmayan dosyaya yazma → hata"       "$out" "Hata.*bulunamadi"

fresh
out=$(run 'create "f.txt"' 'read "f.txt"')
ok "bos dosya okuma → bos mesaji"       "$out" "bos"

# ================================================================
section "APPEND"
# ================================================================

fresh
out=$(run 'create "f.txt"' 'write "f.txt" "A"' 'append "f.txt" "B"' 'read "f.txt"')
ok "append — icerik birlesiyor"         "$out" "AB"

fresh
out=$(run 'create "f.txt"' 'append "f.txt" "ilk"' 'read "f.txt"')
ok "bos dosyaya append"                 "$out" "ilk"

fresh
out=$(run 'create "f.txt"' 'write "f.txt" "X"' 'append "f.txt" "Y"' 'append "f.txt" "Z"' 'read "f.txt"')
ok "coklu append birikimli calisir"     "$out" "XYZ"

fresh
out=$(run 'append "yok.txt" "x"')
ok "olmayan dosyaya append → hata"      "$out" "Hata.*bulunamadi"

# ================================================================
section "RM"
# ================================================================

fresh
out=$(run 'create "sil.txt"' 'rm "sil.txt"' 'ls')
ok "silinen dosya listede yok"          "$out" "bos"

fresh
out=$(run 'create "f.txt"' 'write "f.txt" "test"' 'rm "f.txt"' 'status')
ok "silme sonrasi blok bitmap'te serbest" "$out" "Dolu bloklar.*: 0"

fresh
out=$(run 'create "f.txt"' 'rm "f.txt"' 'status')
ok "silme sonrasi inode serbest birakildi" "$out" "Kullanilan inode.*: 0"

fresh
out=$(run 'rm "yok.txt"')
ok "olmayan dosya silme → hata"         "$out" "Hata.*bulunamadi"

# ================================================================
section "RENAME"
# ================================================================

fresh
out=$(run 'create "eski.txt"' 'write "eski.txt" "x"' 'rename "eski.txt" "yeni.txt"')
ok "rename — inode degismedi mesaji"    "$out" "degismedi"

fresh
out=$(run 'create "eski.txt"' 'rename "eski.txt" "yeni.txt"' 'ls')
ok "rename sonrasi yeni ad listede"     "$out" "yeni.txt"

fresh
out=$(run 'create "eski.txt"' 'rename "eski.txt" "yeni.txt"' 'read "eski.txt"')
ok "rename sonrasi eski ad okunamaz"    "$out" "Hata.*bulunamadi"

fresh
out=$(run 'create "eski.txt"' 'write "eski.txt" "veri"' 'rename "eski.txt" "yeni.txt"' 'read "yeni.txt"')
ok "rename sonrasi veri korunuyor"      "$out" "veri"

fresh
out=$(run 'create "a.txt"' 'create "b.txt"' 'rename "a.txt" "b.txt"')
ok "varolan ada rename → hata"          "$out" "Hata.*zaten var"

# ================================================================
section "MV"
# ================================================================

fresh
out=$(run 'create "k.txt"' 'write "k.txt" "veri"' 'mv "k.txt" "h.txt"' 'read "h.txt"')
ok "mv — veri hedefte korunuyor"        "$out" "veri"

fresh
out=$(run 'create "k.txt"' 'write "k.txt" "x"' 'mv "k.txt" "h.txt"')
ok "mv — yeni inode tahsis edildi"      "$out" "yeni -> inode"

fresh
out=$(run 'create "k.txt"' 'write "k.txt" "x"' 'mv "k.txt" "h.txt"' 'read "k.txt"')
ok "mv sonrasi kaynak okunamaz"         "$out" "Hata.*bulunamadi"

fresh
out=$(run 'create "a.txt"' 'create "b.txt"' 'mv "a.txt" "b.txt"')
ok "varolan ada mv → hata"             "$out" "Hata.*zaten var"

fresh
out=$(run 'create "k.txt"' 'write "k.txt" "x"' 'mv "k.txt" "h.txt"' 'status')
ok "mv sonrasi inode sayisi ayni"       "$out" "Kullanilan inode.*: 1"

# ================================================================
section "CP"
# ================================================================

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "kopya"' 'cp "a.txt" "b.txt"' 'read "b.txt"')
ok "cp — icerik kopyalaniyor"          "$out" "kopya"

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "ozgün"' 'cp "a.txt" "b.txt"' 'write "b.txt" "degisti"' 'read "a.txt"')
ok "cp — kopya degisince kaynak etkilenmiyor" "$out" "ozgün"

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "x"' 'cp "a.txt" "b.txt"' 'status')
ok "cp sonrasi 2 inode kullaniliyor"    "$out" "Kullanilan inode.*: 2"

fresh
out=$(run 'cp "yok.txt" "b.txt"')
ok "olmayan dosya cp → hata"           "$out" "Hata.*bulunamadi"

# ================================================================
section "CONCAT"
# ================================================================

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "AB"' \
          'create "b.txt"' 'write "b.txt" "CD"' \
          'concat "a.txt" "b.txt" "c.txt"' 'read "c.txt"')
ok "concat — icerik dogru birlesiyor"  "$out" "ABCD"

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "X"' \
          'create "b.txt"' 'write "b.txt" "Y"' \
          'concat "a.txt" "b.txt" "c.txt"' 'read "a.txt"')
ok "concat — kaynak a degismedi"       "$out" "^X$"

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "X"' \
          'create "b.txt"' 'write "b.txt" "Y"' \
          'concat "a.txt" "b.txt" "c.txt"' 'read "b.txt"')
ok "concat — kaynak b degismedi"       "$out" "^Y$"

fresh
out=$(run 'concat "yok.txt" "b.txt" "c.txt"')
ok "olmayan kaynak concat → hata"      "$out" "Hata.*bulunamadi"

# ================================================================
section "STAT"
# ================================================================

fresh
out=$(run 'create "s.txt"' 'write "s.txt" "1234"' 'stat "s.txt"')
ok "stat — inode satiri var"           "$out" "Inode"
ok "stat — bloklar satiri var"         "$out" "Bloklar"
ok "stat — boyut 4 byte"               "$out" "Boyut.*4"

fresh
out=$(run 'create "s.txt"' 'stat "s.txt"')
ok "stat — bos dosyada blok sayisi 0"  "$out" "Blok sayisi.*: 0"

fresh
out=$(run 'stat "yok.txt"')
ok "olmayan dosya stat → hata"         "$out" "Hata.*bulunamadi"

# ================================================================
section "WC"
# ================================================================

fresh
out=$(run 'create "w.txt"' 'write "w.txt" "bir iki uc"' 'wc "w.txt"')
ok "wc — kelime sayisi dogru (3)"      "$out" "3 kelime"
ok "wc — byte sayisi dogru (10)"       "$out" "10 byte"

fresh
out=$(run 'create "w.txt"' 'write "w.txt" "tek"' 'wc "w.txt"')
ok "wc — tek kelime"                   "$out" "1 kelime"
ok "wc — 3 byte"                       "$out" "3 byte"

fresh
out=$(run 'create "w.txt"' 'write "w.txt" "a"' 'wc "w.txt"')
ok "wc — minimum 1 byte"               "$out" "1 byte"

fresh
out=$(run 'wc "yok.txt"')
ok "olmayan dosya wc → hata"           "$out" "Hata.*bulunamadi"

# ================================================================
section "FIND"
# ================================================================

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "anahtar kelime burada"' \
          'create "b.txt"' 'write "b.txt" "alakasiz icerik"' \
          'find "anahtar"')
ok "find — eslesen dosyayi buluyor"    "$out" "a.txt"

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "anahtar kelime"' \
          'create "b.txt"' 'write "b.txt" "alakasiz icerik"' \
          'find "anahtar"')
ok  "find — eslesen dosyayi buluyor"   "$out" "a.txt"
ok  "find — eslesmeyeni atliyor"       "$out" "1 dosyada bulundu"

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "merhaba"' 'find "xyz"')
ok "find — bulamazsa haber veriyor"    "$out" "bulunamadi"

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "test merhaba"' \
          'create "b.txt"' 'write "b.txt" "test dunya"' \
          'create "c.txt"' 'write "c.txt" "alakasiz"' \
          'find "test"')
ok "find — birden fazla eslesen"       "$out" "2 dosyada bulundu"

# ================================================================
section "DUMP"
# ================================================================

fresh
out=$(run 'create "d.txt"' 'write "d.txt" "ABC"' 'dump "d.txt"')
ok "dump — hex degeri dogru (A=41)"   "$out" "41 42 43"
ok "dump — ascii sutunu var"          "$out" "|ABC"
ok "dump — offset 0000 gozukuyor"     "$out" "0000"
ok "dump — dosya boyutu baslikta"     "$out" "3 byte"

fresh
out=$(run 'dump "yok.txt"')
ok "olmayan dosya dump → hata"        "$out" "Hata.*bulunamadi"

# ================================================================
section "DEFRAG"
# ================================================================

fresh
out=$(run 'defrag')
ok "bos diskte defrag calisir"        "$out" "Dosya yok"

# Fragmantasyon: A(blok 8), B(blok 9), C(blok 10) olustur
# B'yi sil → blok 9 bos (araya bosluk girdi)
# D icin 2 blok gereken veri yaz → blok 9 ve 11 (parcali)
# Defrag → D ardisik bloklara tasiniyor
LONG=$(printf '%1025s' | tr ' ' 'X')
fresh
out=$(run \
    'create "a.txt"' 'write "a.txt" "AAA"' \
    'create "b.txt"' 'write "b.txt" "BBB"' \
    'create "c.txt"' 'write "c.txt" "CCC"' \
    'rm "b.txt"' \
    'create "d.txt"' "write \"d.txt\" \"$LONG\"" \
    'defrag' \
    'ls' \
    'read "a.txt"' 'read "c.txt"' 'read "d.txt"')
ok "defrag sonrasi a.txt okunabiliyor"  "$out" "AAA"
ok "defrag sonrasi c.txt okunabiliyor"  "$out" "CCC"
ok "defrag sonrasi d.txt okunabiliyor"  "$out" "XXX"
ok "defrag tamamlandi mesaji"           "$out" "tamamlandi"
ok "defrag sonrasi bloklar ardisik"     "$out" "d.txt.*\[9,10\]"

# ================================================================
section "FSCK"
# ================================================================

fresh
out=$(run 'fsck')
ok "bos disk tutarli"                  "$out" "Hata bulunamadi"

fresh
out=$(run 'create "a.txt"' 'write "a.txt" "x"' 'rm "a.txt"' 'fsck')
ok "olustur → yaz → sil sonrasi tutarli" "$out" "Hata bulunamadi"

fresh
out=$(run \
    'create "a.txt"' 'write "a.txt" "x"' \
    'cp "a.txt" "b.txt"' \
    'mv "b.txt" "c.txt"' \
    'defrag' 'fsck')
ok "cp + mv + defrag sonrasi tutarli"  "$out" "Hata bulunamadi"

fresh
out=$(run \
    'create "a.txt"' 'write "a.txt" "x"' \
    'create "b.txt"' 'write "b.txt" "y"' \
    'rename "a.txt" "r.txt"' \
    'fsck')
ok "rename sonrasi tutarli"            "$out" "Hata bulunamadi"

fresh
out=$(run \
    'create "a.txt"' 'write "a.txt" "aa"' \
    'create "b.txt"' 'write "b.txt" "bb"' \
    'create "c.txt"' 'write "c.txt" "cc"' \
    'rm "b.txt"' \
    'fsck')
ok "ortadaki silme sonrasi tutarli"    "$out" "Hata bulunamadi"

# ================================================================
section "STATUS"
# ================================================================

fresh
out=$(run 'status')
ok "status — disk dosyasi gozukuyor"   "$out" "virtual_disk.bin"
ok "status — toplam kapasite 1024 KB"  "$out" "1024 KB"
ok "status — blok boyutu 1024 byte"    "$out" "Blok boyutu.*1024"

fresh
out=$(run 'create "f.txt"' 'write "f.txt" "test"' 'status')
ok "status — yazma sonrasi dolu blok"  "$out" "Dolu bloklar.*: 1"
ok "status — yazma sonrasi veri 4 B"   "$out" "Toplam veri.*4"
ok "status — inode sayisi 1"           "$out" "Kullanilan inode.*: 1"

fresh
out=$(run 'create "f.txt"' 'write "f.txt" "x"' 'rm "f.txt"' 'status')
ok "silme sonrasi dolu blok 0"         "$out" "Dolu bloklar.*: 0"
ok "silme sonrasi inode 0"             "$out" "Kullanilan inode.*: 0"

# ================================================================
printf "\n${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"
if [ "$FAIL" -eq 0 ]; then
    printf "  ${GREEN}${BOLD}TUM TESTLER GECTI: %d / %d${NC}\n" "$PASS" "$TOTAL"
else
    printf "  ${GREEN}GECTI${NC} : %d\n" "$PASS"
    printf "  ${RED}KALDI${NC} : %d\n" "$FAIL"
    printf "  TOPLAM : %d\n" "$TOTAL"
fi
printf "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"

exit "$FAIL"
