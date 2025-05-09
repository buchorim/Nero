#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>

// Versi compiler
#define VERSION "1.1.03"

// Konstanta untuk batas
#define MAX_VARIABLES 20
#define BUFFER_SIZE 1024
#define CODE_SIZE 16384
#define STRING_SIZE 16384
#define VAR_NAME_LEN 32

// ELF header untuk AArch64
unsigned char elf_header[] = {
    0x7F, 0x45, 0x4C, 0x46, 0x02, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0xB7, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x40, 0x00,
    0x38, 0x00,
    0x01, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00
};

// Program header
unsigned char prog_header[] = {
    0x01, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Struktur variabel
struct Var {
    char nama[VAR_NAME_LEN];
    int reg;
    int dilindungi; // 0 = biasa, 1 = terkunci
} variabel[MAX_VARIABLES] = {0};
int jumlah_variabel = 0;

// Buffer sementara untuk SimpanKe
char buffer_simpan[BUFFER_SIZE] = {0};
int panjang_buffer = 0;

// Debugging flag
int debug_mode = 0;

// Fungsi logging
void log_debug(const char *fmt, ...) {
    if (!debug_mode) return;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[DEBUG] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

// Fungsi bantu: ambil atau buat register untuk variabel
int ambil_reg_variabel(const char *nama) {
    // Cek duplikat
    for (int i = 0; i < jumlah_variabel; i++) {
        if (strcmp(variabel[i].nama, nama) == 0) {
            log_error("Variabel '%s' sudah dideklarasikan", nama);
            return variabel[i].reg;
        }
    }
    // Tambah variabel baru
    if (jumlah_variabel >= MAX_VARIABLES) {
        log_error("Terlalu banyak variabel dideklarasikan. Batas: %d", MAX_VARIABLES);
        return -1;
    }
    strncpy(variabel[jumlah_variabel].nama, nama, VAR_NAME_LEN - 1);
    variabel[jumlah_variabel].nama[VAR_NAME_LEN - 1] = '\0';
    variabel[jumlah_variabel].reg = jumlah_variabel;
    variabel[jumlah_variabel].dilindungi = 0;
    log_debug("Variabel baru '%s' dialokasikan ke reg %d", nama, jumlah_variabel);
    return jumlah_variabel++;
}

// Fungsi bantu: cetak teks
void cetak_teks(char *teks, int panjang_teks, int panjang_string, int basis_string, unsigned char *kode, int *panjang_kode, char *string) {
    if (panjang_buffer + panjang_teks >= BUFFER_SIZE) {
        log_error("Buffer overflow: teks '%s' terlalu panjang untuk buffer_simpan", teks);
        return;
    }
    if (panjang_string + panjang_teks >= STRING_SIZE) {
        log_error("String buffer overflow: teks '%s' terlalu panjang", teks);
        return;
    }
    int offset_string = basis_string + panjang_string;
    memcpy(buffer_simpan + panjang_buffer, teks, panjang_teks);
    panjang_buffer += panjang_teks;
    memcpy(string + panjang_string, teks, panjang_teks);
    kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
    kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x01;
    kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = (offset_string >> 16) & 0xFF;
    kode[(*panjang_kode)++] = (offset_string >> 8) & 0xFF; kode[(*panjang_kode)++] = 0x01;
    kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = (panjang_teks >> 16) & 0xFF;
    kode[(*panjang_kode)++] = (panjang_teks >> 8) & 0xFF; kode[(*panjang_kode)++] = 0x02;
    kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
    kode[(*panjang_kode)++] = 0x02; kode[(*panjang_kode)++] = 0x08;
    kode[(*panjang_kode)++] = 0xD4; kode[(*panjang_kode)++] = 0x00;
    kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
    log_debug("Cetak teks '%s' pada offset 0x%X, panjang %d", teks, offset_string, panjang_teks);
}

// Fungsi bantu: simpan ke file
void simpan_ke_file(const char *jalur, int reg, unsigned char *kode, int *panjang_kode) {
    size_t panjang_jalur = strlen(jalur) + 1;
    if (panjang_buffer + panjang_jalur >= BUFFER_SIZE) {
        log_error("Buffer overflow: jalur '%s' terlalu panjang untuk buffer_simpan", jalur);
        return;
    }
    int offset_string = 0x400200 + *panjang_kode;
    memcpy(buffer_simpan + panjang_buffer, jalur, panjang_jalur);
    panjang_buffer += panjang_jalur;
    kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = (offset_string >> 16) & 0xFF;
    kode[(*panjang_kode)++] = (offset_string >> 8) & 0xFF; kode[(*panjang_kode)++] = 0x01;
    kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = (reg >> 16) & 0xFF;
    kode[(*panjang_kode)++] = (reg >> 8) & 0xFF; kode[(*panjang_kode)++] = (reg & 0xFF);
    kode[(*panjang_kode)++] = 0xD4; kode[(*panjang_kode)++] = 0x00;
    kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
    log_debug("Simpan ke file '%s' dari reg %d", jalur, reg);
}

// Fungsi bantu: parse dan generate kode
int parse_perintah(char *potong, int nomor_baris, int indentasi_diharapkan, int *kedalaman_blok, int *atas_label, int *tumpukan_label, int perlambat_aktif, unsigned char *kode, int *panjang_kode, char *string, int *panjang_string, int basis_string, int *ada_kesalahan) {
    char perintah[20], var[VAR_NAME_LEN], teks[100], unit[10], jalur[100], aksi[20];
    int nilai, nilai2, reg;

    if (sscanf(potong, "%19s", perintah) != 1) {
        log_error("Baris %d: Baris kosong atau salah format", nomor_baris);
        *ada_kesalahan = 1;
        return 0;
    }

    log_debug("Parsing perintah: %s", potong);

    // Arithmetic
    if (sscanf(potong, "Atur %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Gagal mengalokasikan variabel '%s'", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        kode[(*panjang_kode)++] = 0xF1;
        kode[(*panjang_kode)++] = (nilai >> 16) & 0xFF;
        kode[(*panjang_kode)++] = (nilai >> 8) & 0xFF;
        kode[(*panjang_kode)++] = (reg & 0x1F);
        kode[(*panjang_kode)++] = 0x14;
        kode[(*panjang_kode)++] = nilai & 0xFF;
        log_debug("Atur %s = %d (reg %d)", var, nilai, reg);
    }
    else if (sscanf(potong, "Tambah %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        kode[(*panjang_kode)++] = 0xD2;
        kode[(*panjang_kode)++] = (nilai >> 16) & 0xFF;
        kode[(*panjang_kode)++] = (nilai >> 8) & 0xFF;
        kode[(*panjang_kode)++] = (nilai & 0xFF) | (reg << 5);
    }
    else if (sscanf(potong, "Kurang %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        kode[(*panjang_kode)++] = 0xD2;
        kode[(*panjang_kode)++] = (nilai >> 16) & 0xFF;
        kode[(*panjang_kode)++] = (nilai >> 8) & 0xFF;
        kode[(*panjang_kode)++] = (nilai & 0xFF) | (reg << 5);
        kode[(*panjang_kode)++] = 0x91;
        kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x01;
        kode[(*panjang_kode)++] = 0x14;
        kode[(*panjang_kode)++] = 0x00;
    }
    else if (sscanf(potong, "Kali %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        kode[(*panjang_kode)++] = 0xD2;
        kode[(*panjang_kode)++] = (nilai >> 16) & 0xFF;
        kode[(*panjang_kode)++] = (nilai >> 8) & 0xFF;
        kode[(*panjang_kode)++] = (nilai & 0xFF) | (reg << 5);
        kode[(*panjang_kode)++] = 0x54;
        kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x01;
    }
    else if (sscanf(potong, "Bagi %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        kode[(*panjang_kode)++] = 0xD2;
        kode[(*panjang_kode)++] = (nilai >> 16) & 0xFF;
        kode[(*panjang_kode)++] = (nilai >> 8) & 0xFF;
        kode[(*panjang_kode)++] = (nilai & 0xFF) | (reg << 5);
        kode[(*panjang_kode)++] = 0x58;
        kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x01;
    }
    // Variable protection
    else if (sscanf(potong, "Amankan %31s", var) == 1) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[reg].dilindungi = 1;
        log_debug("Variabel '%s' diamankan", var);
    }
    else if (sscanf(potong, "Bebaskan %31s", var) == 1) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[reg].dilindungi = 0;
        log_debug("Variabel '%s' dibebaskan", var);
    }
    else if (sscanf(potong, "Pertahankan %31s", var) == 1) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[reg].dilindungi = 1;
        log_debug("Variabel '%s' dipertahankan", var);
    }
    // Random
    else if (sscanf(potong, "Acak %31s %d %d", var, &nilai, &nilai2) == 3) {
        if (nilai > nilai2) {
            log_error("Baris %d: Min harus lebih kecil dari max", nomor_baris);
            *ada_kesalahan = 1;
            return 0;
        }
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        kode[(*panjang_kode)++] = 0xD2;
        kode[(*panjang_kode)++] = (nilai >> 16) & 0xFF;
        kode[(*panjang_kode)++] = (nilai >> 8) & 0xFF;
        kode[(*panjang_kode)++] = (reg & 0x1F);
        kode[(*panjang_kode)++] = 0xD2;
        kode[(*panjang_kode)++] = ((nilai2 - nilai + 1) >> 16) & 0xFF;
        kode[(*panjang_kode)++] = ((nilai2 - nilai + 1) >> 8) & 0xFF;
        kode[(*panjang_kode)++] = 0x01;
        kode[(*panjang_kode)++] = 0xD4;
        kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00;
    }
    // Output
    else if (sscanf(potong, "Tampilkan \"%[^\"]\" %31s", teks, var) == 2 ||
             sscanf(potong, "Tampilkan \"%[^\"]\"", teks) == 1) {
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
        if (strstr(potong, "\" ") && sscanf(potong + strlen("Tampilkan \"") + strlen(teks) + 1, "%31s", var) == 1) {
            reg = ambil_reg_variabel(var);
            if (reg < 0) {
                log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
                *ada_kesalahan = 1;
                return 0;
            }
            kode[(*panjang_kode)++] = 0xD2;
            kode[(*panjang_kode)++] = (reg >> 16) & 0xFF;
            kode[(*panjang_kode)++] = (reg >> 8) & 0xFF;
            kode[(*panjang_kode)++] = (reg & 0xFF);
            kode[(*panjang_kode)++] = 0xD4;
            kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00;
        }
    }
    else if (sscanf(potong, "Tunjukkan \"%[^\"]\" %31s", teks, var) == 2 ||
             sscanf(potong, "Tunjukkan \"%[^\"]\"", teks) == 1) {
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
        if (strstr(potong, "\" ") && sscanf(potong + strlen("Tunjukkan \"") + strlen(teks) + 1, "%31s", var) == 1) {
            reg = ambil_reg_variabel(var);
            if (reg < 0) {
                log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
                *ada_kesalahan = 1;
                return 0;
            }
            kode[(*panjang_kode)++] = 0xD2;
            kode[(*panjang_kode)++] = (reg >> 16) & 0xFF;
            kode[(*panjang_kode)++] = (reg >> 8) & 0xFF;
            kode[(*panjang_kode)++] = (reg & 0xFF);
            kode[(*panjang_kode)++] = 0xD4;
            kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00;
        }
    }
    // Input
    else if (sscanf(potong, "Tanyakan \"%[^\"]\" simpan ke %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
        kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = (reg & 0x1F);
        kode[(*panjang_kode)++] = 0xD4; kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        log_debug("Tanyakan '%s' simpan ke %s (reg %d)", teks, var, reg);
    }
    else if (sscanf(potong, "MasukkanAngka \"%[^\"]\" simpan ke %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
        kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = (reg & 0x1F);
        kode[(*panjang_kode)++] = 0xD4; kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x01;
        log_debug("MasukkanAngka '%s' simpan ke %s (reg %d)", teks, var, reg);
    }
    else if (sscanf(potong, "MasukkanTeks \"%[^\"]\" simpan ke %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
        kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = (reg & 0x1F);
        kode[(*panjang_kode)++] = 0xD4; kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x02;
        log_debug("MasukkanTeks '%s' simpan ke %s (reg %d)", teks, var, reg);
    }
    // File operations
    else if (sscanf(potong, "SimpanKe \"%[^\"]\" dari %31s", jalur, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
            kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x00;
        }
        simpan_ke_file(jalur, reg, kode, panjang_kode);
        *panjang_string += strlen(jalur) + 1;
    }
    // String manipulation
    else if (sscanf(potong, "Pecah \"%[^\"]\" ke huruf %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (variabel[reg].dilindungi) {
            log_error("Baris %d: Variabel '%s' terkunci", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        kode[(*panjang_kode)++] = 0xD2; kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = (reg & 0x1F);
        kode[(*panjang_kode)++] = 0xD4; kode[(*panjang_kode)++] = 0x00;
        kode[(*panjang_kode)++] = 0x00; kode[(*panjang_kode)++] = 0x03;
        *panjang_string += panjang_teks;
        log_debug("Pecah '%s' ke %s (reg %d)", teks, var, reg);
    }
    else {
        log_error("Baris %d: Perintah '%s' tidak dikenali", nomor_baris, perintah);
        *ada_kesalahan = 1;
        return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Cara pakai: %s <file.nero> [--version] [--debug]\n", argv[0]);
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

    FILE *masukan = fopen(argv[1], "r");
    if (!masukan) {
        log_error("Tidak bisa membuka file '%s'", argv[1]);
        return 1;
    }

    unsigned char *kode = malloc(CODE_SIZE);
    if (!kode) {
        log_error("Gagal mengalokasikan memori untuk kode");
        fclose(masukan);
        return 1;
    }
    char *string = malloc(STRING_SIZE);
    if (!string) {
        log_error("Gagal mengalokasikan memori untuk string");
        free(kode);
        fclose(masukan);
        return 1;
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

    char baris[256];
    while (fgets(baris, sizeof(baris), masukan)) {
        nomor_baris++;
        baris[strcspn(baris, "\n")] = 0;

        // Cek indentasi
        int indentasi = 0;
        int i = 0;
        while (baris[i] == ' ') { indentasi++; i++; }
        if (indentasi % 2 != 0 || indentasi != indentasi_diharapkan * 2) {
            log_error("Baris %d: Indentasi harus %d spasi", nomor_baris, indentasi_diharapkan * 2);
            ada_kesalahan = 1;
            continue;
        }
        char *potong = baris + i;
        if (strlen(potong) == 0) continue;
        if (potong[0] == '*') continue;

        // Pake Nero
        if (strcmp(potong, "Pake Nero") == 0) {
            if (nomor_baris != 1) {
                log_error("Baris %d: 'Pake Nero' harus di baris pertama", nomor_baris);
                ada_kesalahan = 1;
            }
            pake_nero_ada = 1;
            indentasi_diharapkan = 0;
        }
        // (Jalan)
        else if (strcmp(potong, "(Jalan)") == 0) {
            if (indentasi != 0) {
                log_error("Baris %d: '(Jalan)' tidak boleh diindentasi", nomor_baris);
                ada_kesalahan = 1;
            }
            jalan_ada = 1;
        }
        else {
            if (!parse_perintah(potong, nomor_baris, indentasi_diharapkan, &kedalaman_blok, &atas_label, tumpukan_label, perlambat_aktif, kode, &panjang_kode, string, &panjang_string, basis_string, &ada_kesalahan)) {
                continue;
            }
        }
    }

    fclose(masukan);

    if (!pake_nero_ada) {
        log_error("'Pake Nero' tidak ditemukan");
        free(kode); free(string);
        return 1;
    }
    if (!jalan_ada) {
        log_error("'(Jalan)' tidak ditemukan");
        free(kode); free(string);
        return 1;
    }
    if (ada_kesalahan) {
        log_error("Kompilasi dibatalkan karena ada kesalahan");
        free(kode); free(string);
        return 1;
    }
    if (kedalaman_blok != 0) {
        log_error("Blok tidak ditutup dengan benar");
        free(kode); free(string);
        return 1;
    }

    FILE *keluaran = fopen("a.out", "wb");
    if (!keluaran) {
        log_error("Tidak bisa membuat file keluaran");
        free(kode); free(string);
        return 1;
    }
    fwrite(elf_header, 1, sizeof(elf_header), keluaran);
    fwrite(prog_header, 1, sizeof(prog_header), keluaran);
    fwrite(kode, 1, panjang_kode, keluaran);
    fwrite(string, 1, panjang_string, keluaran);
    fclose(keluaran);
    chmod("a.out", 0755);
    free(kode); free(string);
    log_debug("Kompilasi selesai, file keluaran 'a.out' dibuat");
    return 0;
}