# Mini Dosya Sistemi Simülatörü

Gerçek disk yerine tek bir binary dosya (`virtual_disk.bin`) üzerinde çalışan, temel dosya sistemi davranışlarını taklit eden bir simülatör.

---

## Disk Düzeni

```
Blok 0      → Bitmap         (1024 byte — her byte bir bloğun dolu/boş durumu)
Blok 1–4    → Inode tablosu  (64 inode × 64 byte = 4096 byte)
Blok 5–7    → Dizin tablosu  (64 girdi × 48 byte = 3072 byte)
Blok 8+     → Veri blokları
```

| Yapı | Açıklama |
|---|---|
| **Bitmap** | Her blok için 1 byte: `0` = boş, `1` = dolu |
| **Inode** | Boyut + 7 blok pointer (maks 7 KB/dosya) |
| **DirEntry** | Dosya adı + inode indeksi eşlemesi |

---

## Derleme

```bash
make
```

Temizlemek için:

```bash
make clean
```

---

## Çalıştırma

```bash
./fs_sim
```

İlk çalıştırmada `virtual_disk.bin` otomatik oluşturulur ve formatlanır.

---

## Komutlar

Tırnak işareti boşluklu isim ve metin için zorunludur.

| Komut | Açıklama |
|---|---|
| `create "ad"` | Boş dosya oluşturur |
| `ls` | Dosyaları boyut ve blok bilgisiyle listeler |
| `write "ad" "metin"` | Dosyaya yazar (üzerine yazar) |
| `append "ad" "metin"` | Dosya sonuna ekler |
| `read "ad"` | Dosya içeriğini gösterir |
| `rm "ad"` | Dosyayı siler, blok ve inode'u serbest bırakır |
| `rename "eski" "yeni"` | Sadece dizin girdisini günceller — inode ve bloklar aynı kalır |
| `mv "kaynak" "hedef"` | Yeni inode ve blok tahsis eder, kaynağı siler |
| `cp "kaynak" "hedef"` | Aynı disk içinde kopyalar |
| `concat "a" "b" "c"` | a ve b içeriğini birleştirip c'ye yazar |
| `stat "ad"` | İnode numarası, blok listesi, boyut |
| `wc "ad"` | Satır / kelime / byte sayısı |
| `find "kelime"` | Tüm dosya içeriklerinde arar |
| `dump "ad"` | Hex + ASCII dump |
| `defrag` | Veri bloklarını DATA_START'tan itibaren sıkıştırır |
| `fsck` | Bitmap ↔ inode tutarsızlıklarını tarar |
| `status` | Disk kapasitesi, boş/dolu blok ve inode özeti |
| `help` | Komut listesi |
| `help "komut"` | Komut detayı, kullanım senaryosu ve örnek |
| `exit` | Çıkış |

### `rename` ile `mv` farkı

```
rename → inode aynı, bloklar aynı, sadece isim değişir   (atomik, hızlı)
mv     → yeni inode tahsis edilir, veri yeni bloklara kopyalanır
```

---

## Testler

```bash
./test_fs.sh          # Sadece PASS / FAIL
./test_fs.sh -v       # Komutlar ve çıktılarla birlikte (verbose)
```

76 test; create, write, append, read, rm, rename, mv, cp, concat, stat, wc, find, dump, defrag, fsck ve status komutlarını kapsar.
