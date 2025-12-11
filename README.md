# Bench - VPS Quick Audit Tool

**Bench** adalah alat audit server ringan (*lightweight*) berbasis C++ modern yang dirancang untuk melakukan pengecekan cepat terhadap spesifikasi sistem, performa disk (I/O), dan kecepatan jaringan (Speedtest) tanpa dependensi yang berat.

Versi ini (**v6.9.6**) dikhususkan untuk **Stabilitas & Keamanan**, menggunakan standar **C++23** yang kompatibel dengan sebagian besar distribusi Linux modern (Ubuntu 25.04+, Debian 13+, Fedora, CentOS Stream 10) dan dikompilasi menggunakan **GCC**.

---

## 🚀 Fitur Utama

### 1. **System Information**
Menampilkan informasi detail perangkat keras dan sistem operasi:
- **CPU:** Model, Cores, Frequency, L3 Cache.
- **Hardware:** Deteksi AES-NI dan Virtualization (VM-x/AMD-V).
- **System:** OS, Kernel, Arsitektur, Uptime, Load Average.
- **Memory:** Penggunaan RAM dan Swap (termasuk deteksi ZRAM/ZSwap).
- **Network:** Cek konektivitas IPv4 & IPv6, ISP, dan Lokasi Geografis.

### 2. **Accurate Disk Benchmark**
- Menggunakan **`O_DIRECT`** syscall untuk menulis file 1GB langsung ke disk.
- **Anti-Tipu:** Memaksa OS untuk mem-bypass RAM Cache (Buffer Cache), sehingga hasil benchmark adalah kecepatan *write* disk yang sesungguhnya.

### 3. **Stable Network Speedtest**
- **Wrapper Cerdas:** Menggunakan binary resmi `speedtest-cli` dari Ookla.
- **CSV Parser:** Membaca output dalam format CSV untuk akurasi data 100% (menghindari error parsing teks).
- **Rate Limit Auto-Stop:** Otomatis berhenti jika IP terdeteksi kena *rate limit* oleh Ookla.
- **Format Rapi:** Output kecepatan dalam **Mbps** dan Latency yang presisi.

### 4. **Security Hardened (Enterprise Ready)**
Dikompilasi dengan flag keamanan standar industri untuk mencegah eksploitasi:
- **ASLR (PIE):** *Position Independent Executable*.
- **Stack Protector Strong:** Mencegah *buffer overflow*.
- **Full RELRO:** *Read-Only Relocations* (mencegah *GOT overwrite*).
- **Fortify Source:** Deteksi buffer overflow saat runtime.

---

## 🛠️ Prerequisites (Persyaratan)

Pastikan compiler C++ (GCC) dan library `libcurl` sudah terinstall.

### Ubuntu / Debian / Kali Linux
```bash
sudo apt update
sudo apt install build-essential cmake libcurl4-openssl-dev
````

### Fedora / RHEL / CentOS / AlmaLinux

```bash
sudo dnf install gcc-c++ cmake libcurl-devel
```

### Arch Linux

```bash
sudo pacman -S base-devel cmake curl
```

-----

## 🏗️ Cara Build (Kompilasi)

Kita menggunakan **CMake** untuk manajemen build yang mudah dan aman.

1.  **Siapkan direktori build:**

    ```bash
    mkdir build
    cd build
    ```

2.  **Konfigurasi Project:**
    (Otomatis mendeteksi GCC dan mengaktifkan flag keamanan)

    ```bash
    cmake ..
    ```

3.  **Compile:**

    ```bash
    make
    ```

4.  **Jalankan:**

    ```bash
    ./bench
    ```

-----

## 📖 Penggunaan

Cukup jalankan binary yang sudah dicompile. Script akan otomatis mendeteksi nama programnya sendiri.

```bash
./bench
```

**Output Contoh:**

```text
------------------------------------------------------------------------------
 A Bench Script (Edition v6.9.6)
 Usage : ./bench
------------------------------------------------------------------------------
 -> CPU & Hardware
 CPU Model            : AMD EPYC 7763 64-Core Processor
 CPU Cores            : 2 @ 3241.8 MHz
 CPU Cache            : 32 MB
 AES-NI               : ✓ Enabled
 VM-x/AMD-V           : ✓ Enabled

 -> System Info
 OS                   : Ubuntu 24.04.3 LTS
 Arch                 : x86_64 (64 Bit)
 Kernel               : 6.8.0-1030-azure
 TCP CC               : cubic
 Virtualization       : Hyper-V
 System Uptime        : 0 days, 2 hour 46 min
 Load Average         : 0.09, 0.14, 0.25

 -> Storage & Memory
 Total Disk           : 31.3 GB (12.1 GB Used)
 Total Mem            : 7.8 GB (2.3 GB Used)

 -> Network
 IPv4/IPv6            : ✓ Online / ✗ Offline
 ISP                  : AS8075 Microsoft Corporation
 Location             : Singapore / SG
------------------------------------------------------------------------------
Running I/O Test (1GB File)...
 I/O Speed (Run #1) : 87.5 MB/s
 I/O Speed (Run #2) : 87.6 MB/s
 I/O Speed (Run #3) : 85.0 MB/s
 I/O Speed (Average) : 86.7 MB/s
------------------------------------------------------------------------------
 Node Name            Upload            Download            Latency     Loss    
 Speedtest.net (Auto) 2028.65 Mbps      9196.19 Mbps        1.08 ms     0%      
 ...
```

-----

## 🧹 Bersih-bersih (Clean Up)

Script ini dirancang untuk "Bersih". Setelah dijalankan, ia akan otomatis menghapus file sementara:

  - `speedtest.tgz` (Installer speedtest)
  - `speedtest-cli/` (Folder ekstraksi)
  - `benchtest_file` (File dummy 1GB untuk tes disk)

Jika ingin membersihkan hasil kompilasi:

```bash
# Dari dalam folder build/
make clean
# Atau hapus folder build
cd .. && rm -rf build
```

-----

## ⚠️ Troubleshooting

**1. Error: `curl/curl.h not found`**
Library curl belum terinstall.

  * Solusi: Install `libcurl4-openssl-dev` (Ubuntu) atau `libcurl-devel` (Fedora).

**2. Error: `Network Timeout` di semua server**
IP Address server kamu mungkin di-blokir oleh Ookla (Geoblocked) atau firewall memblokir port 8080/UDP.

  * Solusi: Coba jalankan di jaringan lain, atau gunakan VPN. Ini bukan bug pada kode.

**3. Error: `GLIBCXX_... not found`**
Kamu mengompilasi di OS baru (misal Ubuntu 24.04) lalu menjalankannya di OS tua (Ubuntu 18.04).

  * Solusi: Compile kode langsung di mesin target tempat kode akan dijalankan.

-----

## 📜 License

Project ini bersifat **Open Source**. Bebas dimodifikasi dan didistribusikan.
Disclaimer: Penulis tidak bertanggung jawab atas penggunaan script ini untuk tujuan yang melanggar hukum.

