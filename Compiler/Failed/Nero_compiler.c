#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

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
    char nama[8];
    int reg;
    int dilindungi; // Untuk Pertahankan
} variabel[10] = {0};
int jumlah_variabel = 0;

// Buffer sementara untuk Simpan
char buffer_simpan[256] = {0};
int panjang_buffer = 0;

// Ambil register untuk variabel
int ambil_reg_variabel(const char *nama) {
    for (int i = 0; i < jumlah_variabel; i++) {
        if (strcmp(variabel[i].nama, nama) == 0) return variabel[i].reg;
    }
    if (jumlah_variabel < 10) {
        strncpy(variabel[jumlah_variabel].nama, nama, 7);
        variabel[jumlah_variabel].nama[7] = '\0';
        variabel[jumlah_variabel].reg = jumlah_variabel;
        variabel[jumlah_variabel].dilindungi = 0;
        return jumlah_variabel++;
    }
    return -1;
}

// Cetak teks ke kode AArch64
void cetak_teks(char *teks, int panjang_teks, int panjang_string, int basis_string, unsigned char *kode, int *panjang_kode, char *string) {
    int offset_string = basis_string + panjang_string;
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
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Cara pakai: %s <file.nero>\n", argv[0]);
        return 1;
    }
    FILE *masukan = fopen(argv[1], "r");
    if (!masukan) {
        printf("Kesalahan: Tidak bisa buka file %s\n", argv[1]);
        return 1;
    }

    unsigned char kode[8192] = {0};
    int panjang_kode = 0;
    char string[8192] = {0};
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
            printf("Kesalahan pada baris %d: Indentasi harus %d spasi\n", nomor_baris, indentasi_diharapkan * 2);
            ada_kesalahan = 1;
            continue;
        }
        char *potong = baris + i;
        if (strlen(potong) == 0) continue;
        if (potong[0] == '*') continue; // Komen

        // Ambil perintah
        char perintah[20];
        if (sscanf(potong, "%19s", perintah) != 1) {
            printf("Kesalahan pada baris %d: Baris kosong atau salah format\n", nomor_baris);
            ada_kesalahan = 1;
            continue;
        }

        char var[10], teks[100], unit[10], jalur[100], aksi[20], teks_error[100];
        int nilai, nilai2, awal, akhir, lama, baru, tingkat, min, max;

        if (strcmp(perintah, "Pake") == 0 && strcmp(potong, "Pake Nero") == 0) {
            if (nomor_baris != 1) {
                printf("Kesalahan pada baris %d: 'Pake Nero' harus di baris pertama\n", nomor_baris);
                ada_kesalahan = 1;
            }
            pake_nero_ada = 1;
            indentasi_diharapkan = 0;
        }
        else if (strcmp(perintah, "(Jalan)") == 0 && strcmp(potong, "(Jalan)") == 0) {
            if (indentasi != 0) {
                printf("Kesalahan pada baris %d: '(Jalan)' tidak boleh diindentasi\n", nomor_baris);
                ada_kesalahan = 1;
            }
            jalan_ada = 1;
        }
        else if (sscanf(potong, "Atur %9s %d", var, &nilai) == 2) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Terlalu banyak variabel\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                // dummy instructions
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            // emit instructions
            kode[panjang_kode++] = 0xF1;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = (reg & 0x1F);
            kode[panjang_kode++] = 0x14;
            kode[panjang_kode++] = nilai & 0xFF;
        }
        else if (sscanf(potong, "Tambah %9s %d", var, &nilai) == 2) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (nilai >> 16) & 0xFF;
            kode[panjang_kode++] = (nilai >> 8) & 0xFF;
            kode[panjang_kode++] = (nilai & 0xFF) | (reg << 5);
        }
        else if (sscanf(potong, "Kurang %9s %d", var, &nilai) == 2) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (nilai >> 16) & 0xFF;
            kode[panjang_kode++] = (nilai >> 8) & 0xFF;
            kode[panjang_kode++] = (nilai & 0xFF) | (reg << 5);
            kode[panjang_kode++] = 0x91;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0x14;
            kode[panjang_kode++] = 0x00;
        }
        else if (sscanf(potong, "Kali %9s %d", var, &nilai) == 2) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (nilai >> 16) & 0xFF;
            kode[panjang_kode++] = (nilai >> 8) & 0xFF;
            kode[panjang_kode++] = (nilai & 0xFF) | (reg << 5);
            kode[panjang_kode++] = 0x54;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
        }
        else if (sscanf(potong, "Bagi %9s %d", var, &nilai) == 2) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (nilai >> 16) & 0xFF;
            kode[panjang_kode++] = (nilai >> 8) & 0xFF;
            kode[panjang_kode++] = (nilai & 0xFF) | (reg << 5);
            kode[panjang_kode++] = 0x58;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
        }
        else if (sscanf(potong, "Tunjukkan \"%[^\"]\" %9s", teks, var) == 2 || sscanf(potong, "Tunjukkan \"%[^\"]\"", teks) == 1) {
            int panjang_teks = strlen(teks) + 1;
            int offset_string = basis_string + panjang_string;
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            memcpy(string + panjang_string, teks, panjang_teks);
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (offset_string >> 16) & 0xFF;
            kode[panjang_kode++] = (offset_string >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            if (strstr(potong, "\" ") && sscanf(potong + strlen("Tunjukkan \"") + strlen(teks) + 1, "%9s", var) == 1) {
                // var was found after text
                int reg = ambil_reg_variabel(var);
                if (reg < 0) {
                    printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                    ada_kesalahan = 1;
                    continue;
                }
                kode[panjang_kode++] = 0xD2;
                kode[panjang_kode++] = (reg >> 16) & 0xFF;
                kode[panjang_kode++] = (reg >> 8) & 0xFF;
                kode[panjang_kode++] = (reg & 0xFF);
                kode[panjang_kode++] = 0xD4;
                kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00;
            } else {
                // No var, just jump
                kode[panjang_kode++] = 0xD4;
                kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00;
            }
            panjang_string += panjang_teks;
        }
        else if (sscanf(potong, "Kalau %9s %d", var, &nilai) == 2) {
            if (nilai != 0 && nilai != 1) {
                printf("Kesalahan pada baris %d: Nilai harus 0 atau 1\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xF1;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = (reg & 0x1F) | 0x80;
            kode[panjang_kode++] = 0x54;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
            int pos_label = panjang_kode - 4;
            panjang_string += strlen("Maka:")+1; // dummy effect
            int offset = panjang_kode - pos_label;
            kode[pos_label + 1] = (offset >> 2) & 0xFF;
            kode[pos_label + 2] = (offset >> 10) & 0xFF;
        }
        else if (sscanf(potong, "KalauBukan %9s %d", var, &nilai) == 2) {
            if (nilai != 0 && nilai != 1) {
                printf("Kesalahan pada baris %d: Nilai harus 0 atau 1\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xF1;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = (reg & 0x1F) | 0x80;
            kode[panjang_kode++] = 0x54;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
            int pos_label = panjang_kode - 4;
            panjang_string += strlen("Maka:")+1;
            int offset = panjang_kode - pos_label;
            kode[pos_label + 1] = (offset >> 2) & 0xFF;
            kode[pos_label + 2] = (offset >> 10) & 0xFF;
        }
        else if (sscanf(potong, "Ulangi %d", &nilai) == 1) {
            if (nilai < 0) {
                printf("Kesalahan pada baris %d: Nilai harus non-negatif\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (nilai >> 16) & 0xFF;
            kode[panjang_kode++] = (nilai >> 8) & 0xFF;
            kode[panjang_kode++] = 0x09;
        }
        else if (sscanf(potong, "Sampai %9s %d", var, &nilai) == 2) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xF1;
            kode[panjang_kode++] = (nilai >> 16) & 0xFF;
            kode[panjang_kode++] = (nilai >> 8) & 0xFF;
            kode[panjang_kode++] = (reg & 0x1F) | 0x80;
            kode[panjang_kode++] = 0x54;
            kode[panjang_kode++] = 0xFF;
            kode[panjang_kode++] = 0xFF;
            kode[panjang_kode++] = 0x00;
            tumpukan_label[++atas_label] = panjang_kode - 4;
            kedalaman_blok++;
            indentasi_diharapkan++;
        }
        else if (sscanf(potong, "Tunggu %d", &nilai) == 1) {
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (nilai >> 16) & 0xFF;
            kode[panjang_kode++] = (nilai >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x08;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
        }
        else if (sscanf(potong, "Tunggu %d waktuhabis %d %9s", &nilai, &nilai2, unit) == 3) {
            if (strcmp(unit, "detik") != 0 && strcmp(unit, "menit") != 0) {
                printf("Kesalahan pada baris %d: Unit harus detik atau menit\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            int detik = nilai2 * (strcmp(unit, "menit") == 0 ? 60 : 1);
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (detik >> 16) & 0xFF;
            kode[panjang_kode++] = (detik >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x08;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
        }
        else if (strcmp(perintah, "Tunggusambilkerjalain") == 0 && strcmp(potong, "Tunggusambilkerjalain") == 0) {
            char *msg = "Kerja sambil nunggu\n";
            int len = strlen(msg) + 1;
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            cetak_teks(msg, len, panjang_string, basis_string, kode, &panjang_kode, string);
            panjang_string += len;
        }
        else if (sscanf(potong, "Urutkan dari %d ke %d", &awal, &akhir) == 2) {
            if (awal > akhir) {
                printf("Kesalahan pada baris %d: Awal harus lebih kecil dari akhir\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (awal >> 16) & 0xFF;
            kode[panjang_kode++] = (awal >> 8) & 0xFF;
            kode[panjang_kode++] = 0x09;
            int start_loop = panjang_kode;
            kode[panjang_kode++] = 0xF1;
            kode[panjang_kode++] = (akhir >> 16) & 0xFF;
            kode[panjang_kode++] = (akhir >> 8) & 0xFF;
            kode[panjang_kode++] = 0x89 | 0x80;
            kode[panjang_kode++] = 0x54;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
            int pos_label = panjang_kode - 4;
            char nums[16];
            snprintf(nums, sizeof(nums), "%d\n", awal);
            int len = strlen(nums) + 1;
            cetak_teks(nums, len, panjang_string, basis_string, kode, &panjang_kode, string);
            panjang_string += len;
            kode[panjang_kode++] = 0x91;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0x09 | (0x09 << 5);
            kode[panjang_kode++] = 0x14;
            kode[panjang_kode++] = ((start_loop - panjang_kode - 4) >> 2) & 0xFF;
            kode[panjang_kode++] = ((start_loop - panjang_kode - 4) >> 10) & 0xFF;
            kode[panjang_kode++] = 0x00;
            int offset_loop = panjang_kode - pos_label;
            kode[pos_label + 1] = (offset_loop >> 2) & 0xFF;
            kode[pos_label + 2] = (offset_loop >> 10) & 0xFF;
        }
        else if (sscanf(potong, "Kosong \"%[^\"]\"", teks) == 1) {
            int len = strlen(teks) + 1;
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (strlen(teks) == 0 ? 1 : 0) >> 16 & 0xFF;
            kode[panjang_kode++] = (strlen(teks) == 0 ? 1 : 0) >> 8 & 0xFF;
            kode[panjang_kode++] = 0x09;
        }
        else if (sscanf(potong, "Lihat \"%[^\"]\"", jalur) == 1) {
            int len = strlen(jalur) + 1;
            int offset_str = basis_string + panjang_string;
            memcpy(string + panjang_string, jalur, len);
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (offset_str >> 16) & 0xFF;
            kode[panjang_kode++] = (offset_str >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x09;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            panjang_string += len;
        }
        else if (sscanf(potong, "Buatfolder \"%[^\"]\"", jalur) == 1) {
            int len = strlen(jalur) + 1;
            int offset_str = basis_string + panjang_string;
            memcpy(string + panjang_string, jalur, len);
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (offset_str >> 16) & 0xFF;
            kode[panjang_kode++] = (offset_str >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xFF;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x08;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            panjang_string += len;
        }
        else if (sscanf(potong, "Buatfile \"%[^\"]\"", jalur) == 1) {
            int len = strlen(jalur) + 1;
            int offset_str = basis_string + panjang_string;
            memcpy(string + panjang_string, jalur, len);
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (offset_str >> 16) & 0xFF;
            kode[panjang_kode++] = (offset_str >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x02;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xFF;
            kode[panjang_kode++] = 0x02;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x08;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            panjang_string += len;
        }
        else if (sscanf(potong, "Hapus \"%[^\"]\"", jalur) == 1) {
            int len = strlen(jalur) + 1;
            int offset_str = basis_string + panjang_string;
            memcpy(string + panjang_string, jalur, len);
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (offset_str >> 16) & 0xFF;
            kode[panjang_kode++] = (offset_str >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x08;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            panjang_string += len;
        }
        else if (sscanf(potong, "Rumah %9s %9s \"%[^\"]\" KalauBukan \"%[^\"]\"", var, aksi, teks, teks_error) == 4) {
            if (strcmp(aksi, "tunjukkan") != 0) {
                printf("Kesalahan pada baris %d: Aksi Rumah tidak didukung\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xF1;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = (reg & 0x1F) | 0x80;
            kode[panjang_kode++] = 0x54;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
            int label1 = panjang_kode - 4;
            int len = strlen(teks) + 1;
            cetak_teks(teks, len, panjang_string, basis_string, kode, &panjang_kode, string);
            panjang_string += len;
            kode[panjang_kode++] = 0x14;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            int label2 = panjang_kode - 4;
            int offset = panjang_kode - label1;
            kode[label1 + 1] = (offset >> 2) & 0xFF;
            kode[label1 + 2] = (offset >> 10) & 0xFF;
            len = strlen(teks_error) + 1;
            cetak_teks(teks_error, len, panjang_string, basis_string, kode, &panjang_kode, string);
            panjang_string += len;
            offset = panjang_kode - label2;
            kode[label2 + 1] = (offset >> 2) & 0xFF;
            kode[label2 + 2] = (offset >> 10) & 0xFF;
        }
        else if (sscanf(potong, "Prioritas %9s %d", var, &tingkat) == 2) {
            if (tingkat < 1 || tingkat > 3) {
                printf("Kesalahan pada baris %d: Tingkat prioritas harus 1, 2, atau 3\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (tingkat == 3 ? 0 : reg) >> 16 & 0xFF;
            kode[panjang_kode++] = (tingkat == 3 ? 0 : reg) >> 8 & 0xFF;
            kode[panjang_kode++] = (tingkat == 3 ? 0 : reg);
        }
        else if (sscanf(potong, "Ganti %9s %d %d", var, &lama, &baru) == 3) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xF1;
            kode[panjang_kode++] = (lama >> 16) & 0xFF;
            kode[panjang_kode++] = (lama >> 8) & 0xFF;
            kode[panjang_kode++] = (reg & 0x1F) | 0x80;
            kode[panjang_kode++] = 0x54;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x01;
            int label = panjang_kode - 4;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (baru >> 16) & 0xFF;
            kode[panjang_kode++] = (baru >> 8) & 0xFF;
            kode[panjang_kode++] = (reg & 0x1F) | ((baru & 0xFF) << 5);
            int offset = panjang_kode - label;
            kode[label + 1] = (offset >> 2) & 0xFF;
            kode[label + 2] = (offset >> 10) & 0xFF;
        }
        else if (sscanf(potong, "Hilangkan %9s", var) == 1) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (variabel[reg].dilindungi) {
                printf("Kesalahan pada baris %d: Variabel dilindungi oleh Pertahankan\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = (reg & 0x1F);
        }
        else if (strcmp(perintah, "Istirahat") == 0 && strcmp(potong, "Istirahat") == 0) {
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x08;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
        }
        else if (sscanf(potong, "Paksa \"%[^\"]\"", jalur) == 1) {
            int len = strlen(jalur) + 1;
            int offset_str = basis_string + panjang_string;
            memcpy(string + panjang_string, jalur, len);
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (offset_str >> 16) & 0xFF;
            kode[panjang_kode++] = (offset_str >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x02;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xFF;
            kode[panjang_kode++] = 0x02;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x08;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            panjang_string += len;
        }
        else if (sscanf(potong, "Simpan \"%[^\"]\" %9s", teks, var) == 2) {
            int len = strlen(teks) + 1;
            if (panjang_buffer + len < 256) {
                memcpy(buffer_simpan + panjang_buffer, teks, len);
                panjang_buffer += len;
            }
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            char numstr[16];
            snprintf(numstr, sizeof(numstr), "%d", reg);
            int lnum = strlen(numstr) + 1;
            if (panjang_buffer + lnum < 256) {
                memcpy(buffer_simpan + panjang_buffer, numstr, lnum);
                panjang_buffer += lnum;
            }
        }
        else if (sscanf(potong, "Simpan \"%[^\"]\"", teks) == 1) {
            int len = strlen(teks) + 1;
            if (panjang_buffer + len < 256) {
                memcpy(buffer_simpan + panjang_buffer, teks, len);
                panjang_buffer += len;
            }
        }
        else if (sscanf(potong, "Simpan \"%[^\"]\" %9s %9s", jalur, var, teks) == 3) {
            // interpret as saving buffer to file jalur with var reg
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            int len = strlen(jalur) + 1;
            int offset_str = basis_string + panjang_string;
            memcpy(string + panjang_string, jalur, len);
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (offset_str >> 16) & 0xFF;
            kode[panjang_kode++] = (offset_str >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (reg >> 16) & 0xFF;
            kode[panjang_kode++] = (reg >> 8) & 0xFF;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            panjang_string += len;
        }
        else if (sscanf(potong, "Baca \"%[^\"]\" %9s", jalur, var) == 2) {
            int len = strlen(jalur) + 1;
            int offset_str = basis_string + panjang_string;
            memcpy(string + panjang_string, jalur, len);
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (offset_str >> 16) & 0xFF;
            kode[panjang_kode++] = (offset_str >> 8) & 0xFF;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (reg >> 16) & 0xFF;
            kode[panjang_kode++] = (reg >> 8) & 0xFF;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            panjang_string += len;
        }
        else if (strcmp(perintah, "Eksekusi") == 0 && strcmp(potong, "Eksekusi") == 0) {
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            cetak_teks(buffer_simpan, panjang_buffer + 1, panjang_string, basis_string, kode, &panjang_kode, string);
            panjang_string += panjang_buffer + 1;
            panjang_buffer = 0;
            memset(buffer_simpan, 0, sizeof(buffer_simpan));
        }
        else if (sscanf(potong, "Perlambat %d", &nilai) == 1) {
            perlambat_aktif = nilai;
        }
        else if (strcmp(perintah, "Selalu") == 0 && strcmp(potong, "Selalu") == 0) {
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            tumpukan_label[++atas_label] = panjang_kode;
            kedalaman_blok++;
            indentasi_diharapkan++;
        }
        else if (sscanf(potong, "Pertahankan %9s", var) == 1) {
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            variabel[reg].dilindungi = 1;
        }
        else if (sscanf(potong, "Acak %9s %d %d", var, &min, &max) == 3) {
            if (min > max) {
                printf("Kesalahan pada baris %d: Min harus lebih kecil dari max\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            int reg = ambil_reg_variabel(var);
            if (reg < 0) {
                printf("Kesalahan pada baris %d: Variabel tidak dikenali\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = (min >> 16) & 0xFF;
            kode[panjang_kode++] = (min >> 8) & 0xFF;
            kode[panjang_kode++] = (reg & 0x1F);
            kode[panjang_kode++] = 0xD2;
            kode[panjang_kode++] = ((max - min + 1) >> 16) & 0xFF;
            kode[panjang_kode++] = ((max - min + 1) >> 8) & 0xFF;
            kode[panjang_kode++] = 0x01;
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
        }
        else if (strcmp(perintah, "Selesai") == 0 && strcmp(potong, "Selesai") == 0) {
            if (kedalaman_blok == 0) {
                printf("Kesalahan pada baris %d: Selesai tanpa blok\n", nomor_baris);
                ada_kesalahan = 1;
                continue;
            }
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            int offset = panjang_kode - tumpukan_label[atas_label];
            kode[tumpukan_label[atas_label] + 1] = (offset >> 2) & 0xFF;
            kode[tumpukan_label[atas_label] + 2] = (offset >> 10) & 0xFF;
            atas_label--;
            kedalaman_blok--;
            indentasi_diharapkan--;
        }
        else if (strcmp(perintah, "Berhenti") == 0 && strcmp(potong, "Berhenti") == 0) {
            if (perlambat_aktif) {
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0xD2; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x08;
                kode[panjang_kode++] = 0xD4; kode[panjang_kode++] = 0x00;
                kode[panjang_kode++] = 0x00; kode[panjang_kode++] = 0x00;
            }
            kode[panjang_kode++] = 0xD4;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
            kode[panjang_kode++] = 0x00;
        }
        else {
            printf("Kesalahan pada baris %d: Perintah tidak dikenali\n", nomor_baris);
            ada_kesalahan = 1;
            continue;
        }
    }

    fclose(masukan);

    if (!pake_nero_ada) {
        printf("Kesalahan: 'Pake Nero' tidak ditemukan\n");
        return 1;
    }
    if (!jalan_ada) {
        printf("Kesalahan: '(Jalan)' tidak ditemukan\n");
        return 1;
    }
    if (ada_kesalahan) {
        printf("Kompilasi dibatalkan karena ada kesalahan\n");
        return 1;
    }
    if (kedalaman_blok != 0) {
        printf("Kesalahan: Blok tidak ditutup dengan benar\n");
        return 1;
    }

    // Tulis file keluaran
    FILE *keluaran = fopen("a.out", "wb");
    if (!keluaran) {
        printf("Kesalahan: Tidak bisa buat file keluaran\n");
        return 1;
    }
    fwrite(elf_header, 1, sizeof(elf_header), keluaran);
    fwrite(prog_header, 1, sizeof(prog_header), keluaran);
    fwrite(kode, 1, panjang_kode, keluaran);
    fwrite(string, 1, panjang_string, keluaran);
    fclose(keluaran);
    chmod("a.out", 0755);
    return 0;
}
