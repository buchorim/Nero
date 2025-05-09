#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Versi compiler
#define VERSION "1.1.03.5"

// Konstanta untuk batas
#define MAX_VARIABLES 20
#define BUFFER_SIZE 1024
#define CODE_SIZE 16384
#define STRING_SIZE 16384
#define VAR_NAME_LEN 32
#define LINE_SIZE 256

// ELF header untuk AArch64 (little-endian)
static unsigned char elf_header[] = {
    0x7F, 0x45, 0x4C, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xB7, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x38,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Program header (little-endian)
static unsigned char prog_header[] = {
    0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00};

// Struktur variabel
struct Var {
    char nama[VAR_NAME_LEN];
    int reg;
    int dilindungi; // 0 = biasa, 1 = terkunci
};

// Struktur untuk menyimpan sumber daya yang perlu dibersihkan
struct Resources {
    unsigned char *kode;
    char *string;
    FILE *masukan;
    FILE *keluaran;
};

// Global state
static struct Var variabel[MAX_VARIABLES] = {0};
static int jumlah_variabel = 0;
static char buffer_simpan[BUFFER_SIZE] = {0};
static int panjang_buffer = 0;
static int debug_mode = 0;

// Membersihkan sumber daya sebelum keluar
static void cleanup_resources(struct Resources *res) {
    if (res->kode) {
        free(res->kode);
        res->kode = NULL;
    }
    if (res->string) {
        free(res->string);
        res->string = NULL;
    }
    if (res->masukan) {
        fclose(res->masukan);
        res->masukan = NULL;
    }
    if (res->keluaran) {
        fclose(res->keluaran);
        res->keluaran = NULL;
    }
}

// Mencatat pesan debug jika debug_mode aktif
static void log_debug(const char *fmt, ...) {
    if (!debug_mode) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[DEBUG] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

// Mencatat pesan error dengan format standar
static void log_error(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s:%d: error: ", file, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

// Menambahkan instruksi AArch64 dalam format little-endian
static unsigned char *tambah_kode(unsigned char *kode, uint32_t word) {
    if (!kode) {
        return kode;
    }
    *kode++ = (word >> 0) & 0xFF;
    *kode++ = (word >> 8) & 0xFF;
    *kode++ = (word >> 16) & 0xFF;
    *kode++ = (word >> 24) & 0xFF;
    return kode;
}

// Mengalokasikan atau mendapatkan register untuk variabel
static int ambil_reg_variabel(const char *nama, const char *file, int line) {
    if (!nama) {
        log_error(file, line, "Nama variabel null");
        return -1;
    }
    for (int i = 0; i < jumlah_variabel; i++) {
        if (strncmp(variabel[i].nama, nama, VAR_NAME_LEN) == 0) {
            return variabel[i].reg;
        }
    }
    if (jumlah_variabel >= MAX_VARIABLES) {
        log_error(file, line, "Terlalu banyak variabel, batas: %d", MAX_VARIABLES);
        return -1;
    }
    strncpy(variabel[jumlah_variabel].nama, nama, VAR_NAME_LEN - 1);
    variabel[jumlah_variabel].nama[VAR_NAME_LEN - 1] = '\0';
    variabel[jumlah_variabel].reg = jumlah_variabel + 9; // Mulai dari X9
    variabel[jumlah_variabel].dilindungi = 0;
    log_debug("Variabel baru '%s' dialokasikan ke reg %d", nama,
              variabel[jumlah_variabel].reg);
    return variabel[jumlah_variabel++].reg;
}

// Menghasilkan instruksi untuk mencetak teks ke stdout
static void cetak_teks(char *teks, int panjang_teks, int panjang_string,
                       int basis_string, unsigned char *kode, int *panjang_kode,
                       char *string, const char *file, int line) {
    if (!teks || !kode || !panjang_kode || !string) {
        log_error(file, line, "Parameter null di cetak_teks");
        return;
    }
    if (panjang_teks < 0 || panjang_teks >= BUFFER_SIZE ||
        panjang_string + panjang_teks >= STRING_SIZE ||
        panjang_buffer + panjang_teks >= BUFFER_SIZE) {
        log_error(file, line, "Buffer overflow untuk teks '%s'", teks);
        return;
    }
    int offset_string = basis_string + panjang_string;
    memcpy(buffer_simpan + panjang_buffer, teks, panjang_teks);
    panjang_buffer += panjang_teks;
    memcpy(string + panjang_string, teks, panjang_teks);
    uint32_t instr[] = {
        0xD2800020,                                         // MOVZ X0, #1 (stdout)
        0xD2800001 | ((offset_string & 0xFFFF) << 5),       // MOVZ X1, #offset_string
        0xD2800002 | ((panjang_teks & 0xFFFF) << 5),        // MOVZ X2, #panjang_teks
        0xD2800808,                                         // MOVZ X8, #64 (syscall write)
        0xD4000001                                          // SVC #0
    };
    for (int i = 0; i < 5; i++) {
        kode = tambah_kode(kode, instr[i]);
        *panjang_kode += 4;
    }
    log_debug("Cetak teks '%s' pada offset 0x%X, panjang %d", teks, offset_string,
              panjang_teks);
}

// Menghasilkan instruksi untuk menyimpan data ke file
static void simpan_ke_file(const char *jalur, int reg, unsigned char *kode,
                           int *panjang_kode, char *string, int *panjang_string,
                           int basis_string, const char *file, int line) {
    if (!jalur || !kode || !panjang_kode || !string || !panjang_string) {
        log_error(file, line, "Parameter null di simpan_ke_file");
        return;
    }
    size_t panjang_jalur = strlen(jalur) + 1;
    if (panjang_buffer + panjang_jalur >= BUFFER_SIZE ||
        *panjang_string + panjang_jalur >= STRING_SIZE) {
        log_error(file, line, "Buffer overflow untuk jalur '%s'", jalur);
        return;
    }
    int offset_string = basis_string + *panjang_string;
    memcpy(buffer_simpan + panjang_buffer, jalur, panjang_jalur);
    panjang_buffer += panjang_jalur;
    memcpy(string + *panjang_string, jalur, panjang_jalur);
    *panjang_string += panjang_jalur;
    uint32_t instr[] = {
        0xD2800001 | ((offset_string & 0xFFFF) << 5), // MOVZ X1, #offset_string
        0xD2800800,                                   // MOVZ X0, #64 (O_WRONLY | O_CREAT)
        0xD2800E02,                                   // MOVZ X2, #0644 (mode)
        0xD2800708,                                   // MOVZ X8, #56 (syscall open)
        0xD4000001,                                   // SVC #0
        0xAA0003E0,                                   // MOV X0, X0 (fd)
        0xD2800001 | ((offset_string & 0xFFFF) << 5), // MOVZ X1, #offset_string
        0xD2800002 | ((panjang_jalur & 0xFFFF) << 5), // MOVZ X2, #panjang_jalur
        0xD2800808,                                   // MOVZ X8, #64 (syscall write)
        0xD4000001                                    // SVC #0
    };
    for (int i = 0; i < 10; i++) {
        kode = tambah_kode(kode, instr[i]);
        *panjang_kode += 4;
    }
    log_debug("Simpan ke file '%s' dari reg %d", jalur, reg);
}

// Mem-parsing perintah Nero dan menghasilkan kode mesin AArch64
static int parse_perintah(char *potong, int nomor_baris,
                          int indentasi_diharapkan, int *kedalaman_blok,
                          int *atas_label, int *tumpukan_label,
                          int perlambat_aktif, unsigned char *kode,
                          int *panjang_kode, char *string, int *panjang_string,
                          int basis_string, int *ada_kesalahan,
                          const char *file) {
    char perintah[20], var[VAR_NAME_LEN], teks[100], jalur[100];
    int nilai, nilai2, reg;

    if (!potong || !kode || !panjang_kode || !string || !panjang_string ||
        !ada_kesalahan) {
        log_error(file, nomor_baris, "Parameter null di parse_perintah");
        *ada_kesalahan = 1;
        return 0;
    }
    if (sscanf(potong, "%19s", perintah) != 1) {
        log_error(file, nomor_baris, "Baris kosong atau salah format");
        *ada_kesalahan = 1;
        return 0;
    }
    log_debug("Parsing perintah: %s", potong);

    // Arithmetic
    if (sscanf(potong, "Atur %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr[] = {
            0xD2800000 | ((nilai & 0xFFFF) << 5) | (reg & 0x1F), // MOVZ X<reg>, #nilai[15:0]
            nilai > 0xFFFF
                ? 0xF2A00000 | (((nilai >> 16) & 0xFFFF) << 5) | (reg & 0x1F)
                : 0xD503201F // MOVK atau NOP
        };
        for (int i = 0; i < (nilai > 0xFFFF ? 2 : 1); i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Atur %s = %d (reg %d)", var, nilai, reg);
    } else if (sscanf(potong, "Tambah %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr = 0x91000000 | ((nilai & 0xFFF) << 10) | (reg << 5) |
                         (reg & 0x1F); // ADD X<reg>, X<reg>, #nilai
        kode = tambah_kode(kode, instr);
        *panjang_kode += 4;
        log_debug("Tambah %s += %d (reg %d)", var, nilai, reg);
    } else if (sscanf(potong, "Kurang %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr = 0xD1000000 | ((nilai & 0xFFF) << 10) | (reg << 5) |
                         (reg & 0x1F); // SUB X<reg>, X<reg>, #nilai
        kode = tambah_kode(kode, instr);
        *panjang_kode += 4;
        log_debug("Kurang %s -= %d (reg %d)", var, nilai, reg);
    } else if (sscanf(potong, "Kali %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr[] = {
            0xD2800001 | ((nilai & 0xFFFF) << 5), // MOVZ X1, #nilai
            0x9B017C00 | (reg << 5) | (reg & 0x1F) // MUL X<reg>, X<reg>, X1
        };
        for (int i = 0; i < 2; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Kali %s *= %d (reg %d)", var, nilai, reg);
    } else if (sscanf(potong, "Bagi %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr[] = {
            0xD2800001 | ((nilai & 0xFFFF) << 5), // MOVZ X1, #nilai
            0x9AC certainty10800 | (reg << 5) | (reg & 0x1F) // SDIV X<reg>, X<reg>, X1
        };
        for (int i = 0; i < 2; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Bagi %s /= %d (reg %d)", var, nilai, reg);
    }
    // Variable protection
    else if (sscanf(potong, "Amankan %31s", var) == 1) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0) {
            log_error(file, nomor_baris, "Variabel '%s' tidak dikenali", var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[reg - 9].dilindungi = 1;
        log_debug("Variabel '%s' diamankan", var);
    } else if (sscanf(potong, "Bebaskan %31s", var) == 1) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0) {
            log_error(file, nomor_baris, "Variabel '%s' tidak dikenali", var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[reg - 9].dilindungi = 0;
        log_debug("Variabel '%s' dibebaskan", var);
    } else if (sscanf(potong, "Pertahankan %31s", var) == 1) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0) {
            log_error(file, nomor_baris, "Variabel '%s' tidak dikenali", var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[reg - 9].dilindungi = 1;
        log_debug("Variabel '%s' dipertahankan", var);
    }
    // Random
    else if (sscanf(potong, "Acak %31s %d %d", var, &nilai, &nilai2) == 3) {
        if (nilai > nilai2) {
            log_error(file, nomor_baris, "Min harus lebih kecil dari max");
            *ada_kesalahan = 1;
            return 0;
        }
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        const char *urandom = "/dev/urandom";
        int panjang_urandom = strlen(urandom) + 1;
        if (*panjang_string + panjang_urandom + 4 >= STRING_SIZE) {
            log_error(file, nomor_baris,
                      "String buffer overflow untuk /dev/urandom");
            *ada_kesalahan = 1;
            return 0;
        }
        int offset_urandom = basis_string + *panjang_string;
        memcpy(string + *panjang_string, urandom, panjang_urandom);
        *panjang_string += panjang_urandom;
        int offset_buffer = basis_string + *panjang_string;
        *panjang_string += 4;
        uint32_t instr[] = {
            0xD2800001 | ((offset_urandom & 0xFFFF) << 5), // MOVZ X1, #offset_urandom
            0xD2800000,                                    // MOVZ X0, #0 (O_RDONLY)
            0xD2800708,                                    // MOVZ X8, #56 (syscall open)
            0xD4000001,                                    // SVC #0
            0xAA0003E0,                                    // MOV X0, X0 (fd)
            0xD2800001 | ((offset_buffer & 0xFFFF) << 5),  // MOVZ X1, #buffer
            0xD2800082,                                    // MOVZ X2, #4 (read 4 bytes)
            0xD28007E8,                                    // MOVZ X8, #63 (syscall read)
            0xD4000001,                                    // SVC #0
            0xF9400020 | (reg << 5),                      // LDR X<reg>, [X1]
            0xD2800001 | (((nilai2 - nilai + 1) & 0xFFFF) << 5), // MOVZ X1, #range
            0x9B017C00 | (reg << 5) | (reg & 0x1F),              // MUL X<reg>, X<reg>, X1
            0xD2800001 | ((nilai & 0xFFFF) << 5),                // MOVZ X1, #min
            0x8B010000 | (reg << 5) | (reg & 0x1F)               // ADD X<reg>, X<reg>, X1
        };
        for (int i = 0; i < 13; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Acak %s dari %d sampai %d (reg %d)", var, nilai, nilai2, reg);
    }
    // Output
    else if (sscanf(potong, "Tampilkan \"%[^\"]\" %31s", teks, var) == 2 ||
             sscanf(potong, "Tampilkan \"%[^\"]\"", teks) == 1) {
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error(file, nomor_baris,
                      "String buffer overflow untuk teks '%s'", teks);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode,
                   panjang_kode, string, file, nomor_baris);
        *panjang_string += panjang_teks;
        if (strstr(potong, "\" ") &&
            sscanf(potong + strlen("Tampilkan \"") + strlen(teks) + 1, "%31s",
                   var) == 1) {
            reg = ambil_reg_variabel(var, file, nomor_baris);
            if (reg < 0) {
                log_error(file, nomor_baris, "Variabel '%s' tidak dikenali",
                          var);
                *ada_kesalahan = 1;
                return 0;
            }
        }
    } else if (sscanf(potong, "Tunjukkan \"%[^\"]\" %31s", teks, var) == 2 ||
               sscanf(potong, "Tunjukkan \"%[^\"]\"", teks) == 1) {
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error(file, nomor_baris,
                      "String buffer overflow untuk teks '%s'", teks);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode,
                   panjang_kode, string, file, nomor_baris);
        *panjang_string += panjang_teks;
        if (strstr(potong, "\" ") &&
            sscanf(potong + strlen("Tunjukkan \"") + strlen(teks) + 1, "%31s",
                   var) == 1) {
            reg = ambil_reg_variabel(var, file, nomor_baris);
            if (reg < 0) {
                log_error(file, nomor_baris, "Variabel '%s' tidak dikenali",
                          var);
                *ada_kesalahan = 1;
                return 0;
            }
        }
    }
    // Input
    else if (sscanf(potong, "Tanyakan \"%[^\"]\" simpan ke %31s", teks, var) ==
             2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks + 32 >= STRING_SIZE) {
            log_error(file, nomor_baris,
                      "String buffer overflow untuk teks '%s'", teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode,
                   panjang_kode, string, file, nomor_baris);
        int offset_buffer = basis_string + *panjang_string + panjang_teks;
        *panjang_string += panjang_teks + 32;
        uint32_t instr[] = {
            0xD2800000,                                    // MOVZ X0, #0 (stdin)
            0xD2800001 | ((offset_buffer & 0xFFFF) << 5),  // MOVZ X1, #offset_buffer
            0xD2800402,                                    // MOVZ X2, #32 (buffer size)
            0xD28007E8,                                    // MOVZ X8, #63 (syscall read)
            0xD4000001,                                    // SVC #0
            0xF9000020 | (reg << 5)                       // STR X0, [X1]
        };
        for (int i = 0; i < 6; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Tanyakan '%s' simpan ke %s (reg %d)", teks, var, reg);
    } else if (sscanf(potong, "MasukkanAngka \"%[^\"]\" simpan ke %31s", teks,
                      var) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks + 32 >= STRING_SIZE) {
            log_error(file, nomor_baris,
                      "String buffer overflow untuk teks '%s'", teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode,
                   panjang_kode, string, file, nomor_baris);
        int offset_buffer = basis_string + *panjang_string + panjang_teks;
        *panjang_string += panjang_teks + 32;
        uint32_t instr[] = {
            0xD2800000,                                    // MOVZ X0, #0 (stdin)
            0xD2800001 | ((offset_buffer & 0xFFFF) << 5),  // MOVZ X1, #offset_buffer
            0xD2800402,                                    // MOVZ X2, #32 (buffer size)
            0xD28007E8,                                    // MOVZ X8, #63 (syscall read)
            0xD4000001,                                    // SVC #0
            0xD2800000 | (reg & 0x1F)                     // MOVZ X<reg>, #0
        };
        for (int i = 0; i < 6; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("MasukkanAngka '%s' simpan ke %s (reg %d)", teks, var, reg);
    } else if (sscanf(potong, "MasukkanTeks \"%[^\"]\" simpan ke %31s", teks,
                      var) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks + 32 >= STRING_SIZE) {
            log_error(file, nomor_baris,
                      "String buffer overflow untuk teks '%s'", teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode,
                   panjang_kode, string, file, nomor_baris);
        int offset_buffer = basis_string + *panjang_string + panjang_teks;
        *panjang_string += panjang_teks + 32;
        uint32_t instr[] = {
            0xD2800000,                                    // MOVZ X0, #0 (stdin)
            0xD2800001 | ((offset_buffer & 0xFFFF) << 5),  // MOVZ X1, #offset_buffer
            0xD2800402,                                    // MOVZ X2, #32 (buffer size)
            0xD28007E8,                                    // MOVZ X8, #63 (syscall read)
            0xD4000001,                                    // SVC #0
            0xF9000020 | (reg << 5)                       // STR X0, [X1]
        };
        for (int i = 0; i < 6; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("MasukkanTeks '%s' simpan ke %s (reg %d)", teks, var, reg);
    }
    // File operations
    else if (sscanf(potong, "SimpanKe \"%[^\"]\" dari %31s", jalur, var) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0) {
            log_error(file, nomor_baris, "Variabel '%s' tidak dikenali", var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        simpan_ke_file(jalur, reg, kode, panjang_kode, string, panjang_string,
                       basis_string, file, nomor_baris);
    }
    // String manipulation
    else if (sscanf(potong, "Pecah \"%[^\"]\" ke huruf %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var, file, nomor_baris);
        if (reg < 0 || variabel[reg - 9].dilindungi) {
            log_error(file, nomor_baris, "Variabel '%s' bermasalah", var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error(file, nomor_baris,
                      "String buffer overflow untuk teks '%s'", teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode,
                   panjang_kode, string, file, nomor_baris);
        *panjang_string += panjang_teks;
        uint32_t instr[] = {
            0xD2800000 | (reg & 0x1F), // MOVZ X<reg>, #0
            0xD503201F                 // NOP
        };
        for (int i = 0; i < 2; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Pecah '%s' ke %s (reg %d)", teks, var, reg);
    } else {
        log_error(file, nomor_baris, "Perintah '%s' tidak dikenali", perintah);
        *ada_kesalahan = 1;
        return 0;
    }
    return 1;
}

// Program utama: mengompilasi file Nero menjadi ELF AArch64
int main(int argc, char *argv[]) {
    struct Resources res = {0};
    int return_code = 0;
    const char *file = (argc > 1) ? argv[1] : "nero_compiler";

    if (argc < 2) {
        log_error(file, 0, "Cara pakai: %s <file.nero> [--version] [--debug]",
                  argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "--version") == 0) {
        printf("Nero Compiler versi %s\n", VERSION);
        return 0;
    }
    if (argc > 2 && strcmp(argv[2], "--debug") == 0) {
        debug_mode = 1;
        log_debug("Mode debug diaktifkan");
    }

    res.masukan = fopen(argv[1], "r");
    if (!res.masukan) {
        log_error(file, 0, "Tidak bisa membuka file '%s'", argv[1]);
        return_code = 1;
        goto cleanup;
    }

    res.kode = malloc(CODE_SIZE);
    if (!res.kode) {
        log_error(file, 0, "Gagal mengalokasikan memori untuk kode");
        return_code = 1;
        goto cleanup;
    }
    res.string = malloc(STRING_SIZE);
    if (!res.string) {
        log_error(file, 0, "Gagal mengalokasikan memori untuk string");
        return_code = 1;
        goto cleanup;
    }

    int panjang_kode = 0;
    int panjang_string = 0;
    int basis_string = 0x400200;
    int nomor_baris = 0;
    int ada_kesalahan = 0;
    int pake_nero_ada = 0;
    int jalan_ada = 0;
    int kedalaman_blok = 0;
    int tumpukan_label[100];
    int atas_label = -1;
    int indentasi_diharapkan = 0;
    int perlambat_aktif = 0;
    char baris[LINE_SIZE];

    while (fgets(baris, LINE_SIZE, res.masukan)) {
        nomor_baris++;
        baris[strcspn(baris, "\n")] = 0;
        if (strlen(baris) >= LINE_SIZE - 1) {
            log_error(file, nomor_baris, "Baris terlalu panjang");
            ada_kesalahan = 1;
            continue;
        }

        int indentasi = 0;
        int i = 0;
        while (baris[i] == ' ') {
            indentasi++;
            i++;
        }
        if (indentasi % 2 != 0 || indentasi != indentasi_diharapkan * 2) {
            log_error(file, nomor_baris, "Indentasi harus %d spasi",
                      indentasi_diharapkan * 2);
            ada_kesalahan = 1;
            continue;
        }
        char *potong = baris + i;
        if (strlen(potong) == 0) {
            continue;
        }
        if (potong[0] == '*') {
            continue;
        }

        if (strcmp(potong, "Pake Nero") == 0) {
            if (nomor_baris != 1) {
                log_error(file, nomor_baris,
                          "'Pake Nero' harus di baris pertama");
                ada_kesalahan = 1;
            }
            pake_nero_ada = 1;
            indentasi_diharapkan = 0;
        } else if (strcmp(potong, "(Jalan)") == 0) {
            if (indentasi != 0) {
                log_error(file, nomor_baris,
                          "'(Jalan)' tidak boleh diindentasi");
                ada_kesalahan = 1;
            }
            jalan_ada = 1;
        } else {
            if (!parse_perintah(potong, nomor_baris, indentasi_diharapkan,
                                &kedalaman_blok, &atas_label, tumpukan_label,
                                perlambat_aktif, res.kode, &panjang_kode,
                                res.string, &panjang_string, basis_string,
                                &ada_kesalahan, file)) {
                continue;
            }
        }
    }

    if (!pake_nero_ada) {
        log_error(file, nomor_baris, "'Pake Nero' tidak ditemukan");
        ada_kesalahan = 1;
    }
    if (!jalan_ada) {
        log_error(file, nomor_baris, "'(Jalan)' tidak ditemukan");
        ada_kesalahan = 1;
    }
    if (ada_kesalahan) {
        log_error(file, nomor_baris,
                  "Kompilasi dibatalkan karena ada kesalahan");
        return_code = 1;
        goto cleanup;
    }
    if (kedalaman_blok != 0) {
        log_error(file, nomor_baris, "Blok tidak ditutup dengan benar");
        return_code = 1;
        goto cleanup;
    }

    uint32_t exit_instr[] = {
        0xD2800000, // MOVZ X0, #0 (exit code)
        0xD2800BA8, // MOVZ X8, #93 (syscall exit)
        0xD4000001  // SVC #0
    };
    for (int i = 0; i < 3; i++) {
        res.kode = tambah_kode(res.kode, exit_instr[i]);
        panjang_kode += 4;
    }

    res.keluaran = fopen("a.out", "wb");
    if (!res.keluaran) {
        log_error(file, nomor_baris, "Tidak bisa membuat file keluaran");
        return_code = 1;
        goto cleanup;
    }
    fwrite(elf_header, 1, sizeof(elf_header), res.keluaran);
    fwrite(prog_header, 1, sizeof(prog_header), res.keluaran);
    fwrite(res.kode, 1, panjang_kode, res.keluaran);
    fwrite(res.string, 1, panjang_string, res.keluaran);
    chmod("a.out", 0755);
    log_debug("Kompilasi selesai, file keluaran 'a.out' dibuat");

cleanup:
    cleanup_resources(&res);
    return return_code;
}