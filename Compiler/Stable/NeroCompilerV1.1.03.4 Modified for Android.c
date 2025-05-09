#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>

// Versi compiler
#define VERSION "1.1.03.3"

// Konstanta untuk batas
#define MAX_VARIABLES 20
#define BUFFER_SIZE 1024
#define CODE_SIZE 16384
#define STRING_SIZE 16384
#define VAR_NAME_LEN 32
#define INPUT_BUFFER_SIZE 16

// ELF header untuk AArch64 (little-endian)
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

// Program header (little-endian)
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
    int reg; // Register mulai dari X9
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

// Fungsi bantu: tambah kode dalam format little-endian
unsigned char* tambah_kode(unsigned char* kode, uint32_t word) {
    *kode++ = (word >> 0) & 0xFF;
    *kode++ = (word >> 8) & 0xFF;
    *kode++ = (word >> 16) & 0xFF;
    *kode++ = (word >> 24) & 0xFF;
    return kode;
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
    variabel[jumlah_variabel].reg = 9 + jumlah_variabel; // Mulai dari X9
    variabel[jumlah_variabel].dilindungi = 0;
    log_debug("Variabel baru '%s' dialokasikan ke reg X%d", nama, variabel[jumlah_variabel].reg);
    return variabel[jumlah_variabel++].reg;
}

// Fungsi bantu: cetak teks
void cetak_teks(char *teks, int panjang_teks, int panjang_string, int basis_string, unsigned char *kode, int *panjang_kode, char *string) {
    if (panjang_buffer + panjang_teks >= BUFFER_SIZE || panjang_string + panjang_teks >= STRING_SIZE) {
        log_error("Buffer overflow: teks '%s'", teks);
        return;
    }
    int offset_string = basis_string + panjang_string;
    memcpy(buffer_simpan + panjang_buffer, teks, panjang_teks);
    panjang_buffer += panjang_teks;
    memcpy(string + panjang_string, teks, panjang_teks);
    uint32_t instr[] = {
        0xD2800020, // MOVZ X0, #1 (stdout)
        0xD2800001 | ((offset_string & 0xFFFF) << 5), // MOVZ X1, #offset_string
        0xD2800002 | ((panjang_teks & 0xFFFF) << 5), // MOVZ X2, #panjang_teks
        0xD28007E8, // MOVZ X8, #63 (syscall write)
        0xD4000001  // SVC #0
    };
    for (int i = 0; i < 5; i++) {
        kode = tambah_kode(kode, instr[i]);
        *panjang_kode += 4;
    }
    log_debug("Cetak teks '%s' pada offset 0x%X, panjang %d", teks, offset_string, panjang_teks);
}

// Fungsi bantu: simpan ke file
void simpan_ke_file(const char *jalur, int reg, unsigned char *kode, int *panjang_kode, char *string, int *panjang_string, int basis_string) {
    size_t panjang_jalur = strlen(jalur) + 1;
    if (panjang_buffer + panjang_jalur >= BUFFER_SIZE || *panjang_string + panjang_jalur >= STRING_SIZE) {
        log_error("Buffer overflow: jalur '%s' terlalu panjang", jalur);
        return;
    }
    int offset_string = basis_string + *panjang_string;
    memcpy(buffer_simpan + panjang_buffer, jalur, panjang_jalur);
    panjang_buffer += panjang_jalur;
    memcpy(string + *panjang_string, jalur, panjang_jalur);
    *panjang_string += panjang_jalur;
    uint32_t instr[] = {
        0xD2800001 | ((offset_string & 0xFFFF) << 5), // MOVZ X1, #offset_string (pathname)
        0xD2800800, // MOVZ X0, #64 (flags: O_WRONLY | O_CREAT)
        0xD2800E02, // MOVZ X2, #0644 (mode)
        0xD2800708, // MOVZ X8, #56 (syscall open)
        0xD4000001, // SVC #0
        0xAA0003E0, // MOV X0, X0 (fd returned in X0)
        0xD2800001 | ((offset_string & 0xFFFF) << 5), // MOVZ X1, #offset_string (buffer)
        0xD2800002 | ((panjang_jalur & 0xFFFF) << 5), // MOVZ X2, #panjang_jalur
        0xD28007E8, // MOVZ X8, #63 (syscall write)
        0xD4000001  // SVC #0
    };
    for (int i = 0; i < 10; i++) {
        kode = tambah_kode(kode, instr[i]);
        *panjang_kode += 4;
    }
    log_debug("Simpan ke file '%s' dari reg X%d", jalur, reg);
}

// Fungsi bantu: parse dan generate kode
int parse_perintah(char *potong, int nomor_baris, int indentasi_diharapkan, int *kedalaman_blok, int *atas_label, int *tumpukan_label, int perlambat_aktif, unsigned char *kode, int *panjang_kode, char *string, int *panjang_string, int basis_string, int *ada_kesalahan) {
    char perintah[20], var[VAR_NAME_LEN], teks[100], unit[10], jalur[100];
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
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr[] = {
            0xD2800000 | ((nilai & 0xFFFF) << 5) | (reg & 0x1F), // MOVZ X<reg>, #nilai (low 16 bits)
            0xF2A00000 | (((nilai >> 16) & 0xFFFF) << 5) | (reg & 0x1F) // MOVK X<reg>, #nilai (high 16 bits)
        };
        for (int i = 0; i < 2; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Atur %s = %d (reg X%d)", var, nilai, reg);
    }
    else if (sscanf(potong, "Tambah %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr = 0x91000000 | ((nilai & 0xFFF) << 10) | (reg << 5) | (reg & 0x1F); // ADD X<reg>, X<reg>, #nilai
        kode = tambah_kode(kode, instr);
        *panjang_kode += 4;
        log_debug("Tambah %s += %d (reg X%d)", var, nilai, reg);
    }
    else if (sscanf(potong, "Kurang %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr = 0xD1000000 | ((nilai & 0xFFF) << 10) | (reg << 5) | (reg & 0x1F); // SUB X<reg>, X<reg>, #nilai
        kode = tambah_kode(kode, instr);
        *panjang_kode += 4;
        log_debug("Kurang %s -= %d (reg X%d)", var, nilai, reg);
    }
    else if (sscanf(potong, "Kali %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr[] = {
            0xD2800003 | ((nilai & 0xFFFF) << 5), // MOVZ X3, #nilai
            0x9B037C00 | (reg << 5) | (reg & 0x1F) // MUL X<reg>, X<reg>, X3
        };
        for (int i = 0; i < 2; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Kali %s *= %d (reg X%d)", var, nilai, reg);
    }
    else if (sscanf(potong, "Bagi %31s %d", var, &nilai) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        uint32_t instr[] = {
            0xD2800003 | ((nilai & 0xFFFF) << 5), // MOVZ X3, #nilai
            0x9AC30800 | (reg << 5) | (reg & 0x1F) // SDIV X<reg>, X<reg>, X3
        };
        for (int i = 0; i < 2; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Bagi %s /= %d (reg X%d)", var, nilai, reg);
    }
    // Variable protection
    else if (sscanf(potong, "Amankan %31s", var) == 1) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[jumlah_variabel-1].dilindungi = 1;
        log_debug("Variabel '%s' diamankan (reg X%d)", var, reg);
    }
    else if (sscanf(potong, "Bebaskan %31s", var) == 1) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[jumlah_variabel-1].dilindungi = 0;
        log_debug("Variabel '%s' dibebaskan (reg X%d)", var, reg);
    }
    else if (sscanf(potong, "Pertahankan %31s", var) == 1) {
        reg = ambil_reg_variabel(var);
        if (reg < 0) {
            log_error("Baris %d: Variabel '%s' tidak dikenali", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        variabel[jumlah_variabel-1].dilindungi = 1;
        log_debug("Variabel '%s' dipertahankan (reg X%d)", var, reg);
    }
    // Random
    else if (sscanf(potong, "Acak %31s %d %d", var, &nilai, &nilai2) == 3) {
        if (nilai > nilai2) {
            log_error("Baris %d: Min harus lebih kecil dari max", nomor_baris);
            *ada_kesalahan = 1;
            return 0;
        }
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        if (perlambat_aktif) {
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        char urandom[] = "/dev/urandom\0";
        size_t len_urandom = strlen(urandom) + 1;
        if (*panjang_string + len_urandom >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk /dev/urandom", nomor_baris);
            *ada_kesalahan = 1;
            return 0;
        }
        int offset_urandom = basis_string + *panjang_string;
        memcpy(string + *panjang_string, urandom, len_urandom);
        *panjang_string += len_urandom;
        uint32_t instr[] = {
            0xD2800001 | ((offset_urandom & 0xFFFF) << 5), // MOVZ X1, #offset_urandom
            0xD2800000, // MOVZ X0, #0 (O_RDONLY)
            0xD2800708, // MOVZ X8, #56 (syscall open)
            0xD4000001, // SVC #0
            0xAA0003E0, // MOV X0, X0 (fd in X0)
            0xD2800001 | (((basis_string + *panjang_string) & 0xFFFF) << 5), // MOVZ X1, #buffer_offset
            0xD2800082, // MOVZ X2, #4 (read 4 bytes)
            0xD28007E8, // MOVZ X8, #63 (syscall read)
            0xD4000001, // SVC #0
            0xD2800003 | ((nilai2 - nilai + 1) << 5), // MOVZ X3, #range
            0x9AC30800 | (reg << 5) | (reg & 0x1F) // SDIV X<reg>, X<reg>, X3 (simplified modulo)
        };
        for (int i = 0; i < 10; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        log_debug("Acak %s dari %d sampai %d (reg X%d)", var, nilai, nilai2, reg);
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
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
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
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
    }
    // Input
    else if (sscanf(potong, "Tanyakan \"%[^\"]\" simpan ke %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks + INPUT_BUFFER_SIZE >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
        uint32_t instr[] = {
            0xD2800000, // MOVZ X0, #0 (stdin)
            0xD2800001 | (((basis_string + *panjang_string) & 0xFFFF) << 5), // MOVZ X1, #buffer_offset
            0xD2800202, // MOVZ X2, #16 (buffer size)
            0xD28007E8, // MOVZ X8, #63 (syscall read)
            0xD4000001 // SVC #0
        };
        for (int i = 0; i < 5; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        *panjang_string += INPUT_BUFFER_SIZE;
        log_debug("Tanyakan '%s' simpan ke %s (reg X%d)", teks, var, reg);
    }
    else if (sscanf(potong, "MasukkanAngka \"%[^\"]\" simpan ke %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks + INPUT_BUFFER_SIZE >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
        uint32_t instr[] = {
            0xD2800000, // MOVZ X0, #0 (stdin)
            0xD2800001 | (((basis_string + *panjang_string) & 0xFFFF) << 5), // MOVZ X1, #buffer_offset
            0xD2800202, // MOVZ X2, #16 (buffer size)
            0xD28007E8, // MOVZ X8, #63 (syscall read)
            0xD4000001, // SVC #0
            0xD2800000 | (reg & 0x1F) // MOVZ X<reg>, #0 (placeholder for conversion)
        };
        for (int i = 0; i < 6; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        *panjang_string += INPUT_BUFFER_SIZE;
        log_debug("MasukkanAngka '%s' simpan ke %s (reg X%d)", teks, var, reg);
    }
    else if (sscanf(potong, "MasukkanTeks \"%[^\"]\" simpan ke %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
            *ada_kesalahan = 1;
            return 0;
        }
        int panjang_teks = strlen(teks) + 1;
        if (*panjang_string + panjang_teks + INPUT_BUFFER_SIZE >= STRING_SIZE) {
            log_error("Baris %d: String buffer overflow untuk teks '%s'", nomor_baris, teks);
            *ada_kesalahan = 1;
            return 0;
        }
        cetak_teks(teks, panjang_teks, *panjang_string, basis_string, kode, panjang_kode, string);
        *panjang_string += panjang_teks;
        uint32_t instr[] = {
            0xD2800000, // MOVZ X0, #0 (stdin)
            0xD2800001 | (((basis_string + *panjang_string) & 0xFFFF) << 5), // MOVZ X1, #buffer_offset
            0xD2800202, // MOVZ X2, #16 (buffer size)
            0xD28007E8, // MOVZ X8, #63 (syscall read)
            0xD4000001 // SVC #0
        };
        for (int i = 0; i < 5; i++) {
            kode = tambah_kode(kode, instr[i]);
            *panjang_kode += 4;
        }
        *panjang_string += INPUT_BUFFER_SIZE;
        log_debug("MasukkanTeks '%s' simpan ke %s (reg X%d)", teks, var, reg);
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
            kode = tambah_kode(kode, 0xD2800000);
            *panjang_kode += 4;
        }
        simpan_ke_file(jalur, reg, kode, panjang_kode, string, panjang_string, basis_string);
    }
    // String manipulation
    else if (sscanf(potong, "Pecah \"%[^\"]\" ke huruf %31s", teks, var) == 2) {
        reg = ambil_reg_variabel(var);
        if (reg < 0 || variabel[jumlah_variabel-1].dilindungi) {
            log_error("Baris %d: Variabel '%s' bermasalah", nomor_baris, var);
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
        uint32_t instr = 0xD2800000 | (reg & 0x1F); // MOVZ X<reg>, #0 (placeholder)
        kode = tambah_kode(kode, instr);
        *panjang_kode += 4;
        log_debug("Pecah '%s' ke %s (reg X%d, placeholder)", teks, var, reg);
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

    // Tambahkan instruksi exit
    uint32_t exit_instr[] = {
        0xD2800000, // MOVZ X0, #0 (exit code)
        0xD2800BA8, // MOVZ X8, #93 (syscall exit)
        0xD4000001 // SVC #0
    };
    for (int i = 0; i < 3; i++) {
        kode = tambah_kode(kode, exit_instr[i]);
        *panjang_kode += 4;
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