#!/usr/bin/env bash
# Kompoz dependency installer (Bash 4+)
set -eo pipefail
lang=en
trmsg(){ if [[ $lang == tr ]]; then printf '%s' "$2"; else printf '%s' "$1"; fi; }
say(){ trmsg "$1" "$2"; printf '\n'; }
ask(){ local a; while true; do printf '%s %s ' "$(trmsg "$1" "$2")" "$(trmsg '[y/N]' '[e/H]')" >&2; read -r a || exit 1; case ${a,,} in y|yes|e|evet) return 0;; n|no|h|hayır|hayir|'') return 1;; esac; done; }
readval(){ printf '%s ' "$(trmsg "$1" "$2")" >&2; read -r REPLY || exit 1; }
command -v sudo >/dev/null && SUDO=(sudo) || SUDO=()
[[ $EUID == 0 ]] && SUDO=()
if [[ $(uname -s) != Linux ]]; then say 'Only Linux hosts are supported.' 'Yalnızca Linux ana makineleri desteklenir.'; exit 1; fi
if command -v pacman >/dev/null; then pm=arch; install=(pacman -S --needed); core=(cmake make freetype2 libpng pkgconf); vk=(vulkan-headers); wl=(libxkbcommon wayland); x11=(libxkbcommon libx11 libxi libxcursor libxrandr); hb=harfbuzz; curl=curl; webp=libwebp; jpeg=libjpeg-turbo; dbus=dbus
elif command -v apt-get >/dev/null; then pm=apt; install=(apt-get install); core=(cmake make libfreetype-dev libpng-dev pkg-config); vk=(libvulkan-dev); wl=(libxkbcommon-dev libwayland-dev); x11=(libxkbcommon-dev libx11-dev libxi-dev libxcursor-dev libxrandr-dev); hb=libharfbuzz-dev; curl=libcurl4-openssl-dev; webp=libwebp-dev; jpeg=libturbojpeg0-dev; dbus=libdbus-1-3
elif command -v dnf >/dev/null; then pm=fedora; install=(dnf install); core=(cmake make freetype-devel libpng-devel pkgconf-pkg-config); vk=(vulkan-headers); wl=(libxkbcommon-devel wayland-devel); x11=(libxkbcommon-devel libX11-devel libXi-devel libXcursor-devel libXrandr-devel); hb=harfbuzz-devel; curl=libcurl-devel; webp=libwebp-devel; jpeg=libjpeg-turbo-devel; dbus=dbus-libs
else say 'Unsupported package manager.' 'Desteklenmeyen paket yöneticisi.'; exit 1; fi
say 'Kompoz dependency installer' 'Kompoz bağımlılık kurucusu'
say 'Uses configured repositories only; no third-party repositories are added.' 'Yalnızca yapılandırılmış depolar kullanılır; üçüncü taraf depo eklenmez.'
if [[ $pm == apt ]]; then if ask 'Refresh APT package indexes?' 'APT paket indeksleri güncellensin mi?'; then "${SUDO[@]}" apt-get update; fi; fi
if [[ $pm == arch ]]; then say 'Arch: update your system with pacman -Syu before proceeding; partial upgrades are unsupported.' 'Arch: devam etmeden önce pacman -Syu ile sistemi güncelleyin; kısmi güncellemeler desteklenmez.'; fi
# (The package managers' own output is in the user's language; LC_ALL=C keeps the fields read here in English.)
candidate(){ case $pm in apt) LC_ALL=C apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ {print $2; exit}';; arch) LC_ALL=C pacman -Si "$1" 2>/dev/null | awk '/^Version/ {print $3; exit}';; fedora) LC_ALL=C dnf -q repoquery --latest-limit 1 --qf '%{version}\n' "$1" 2>/dev/null | head -n1;; esac; }
major(){ local v=${1#*:}; [[ $v =~ ^([0-9]+) ]] && printf '%s' "${BASH_REMATCH[1]}" || true; }
packages=(); compiler_pkgs=()
while true; do
 say 'Compiler: 1) GCC  2) Clang  3) Existing compiler  0) Exit' 'Derleyici: 1) GCC  2) Clang  3) Mevcut derleyici  0) Çıkış'
 readval 'Choice:' 'Seçim:'; compiler_pkgs=(); mode=$REPLY
 case $REPLY in
 0) exit 0;;
 1) family=gcc; cc=gcc; cxx=g++;;
 2) family=clang; cc=clang; cxx=clang++;;
 3) readval 'C++ compiler executable or path:' 'C++ derleyici komutu veya yolu:'; cxx=$REPLY
  if ! command -v "$cxx" >/dev/null || ! banner=$("$cxx" --version 2>/dev/null); then say 'Not a working compiler.' 'Çalışan bir derleyici değil.'; continue; fi
  if [[ $banner == *clang* ]]; then family=clang; version=$("$cxx" -dumpversion 2>/dev/null || true); cc=${cxx/clang++/clang}
  else family=gcc; version=$("$cxx" -dumpfullversion -dumpversion 2>/dev/null || true); cc=${cxx/g++/gcc}; fi
  # (CMake's C compiler, the C++ one's sibling: gcc for g++-14 is gcc-14, clang for clang++; none where there is no such.)
  [[ $cc != "$cxx" ]] && command -v "$cc" >/dev/null || cc=''
  m=$(major "$version");;
 *) continue;; esac
 if [[ $mode == 1 || $mode == 2 ]]; then
  say 'Enter a major version (e.g. 14, 17, 22) or auto for the repository default.' 'Ana sürümü girin (örn. 14, 17, 22) veya depo varsayılanı için auto yazın.'
  readval 'Version:' 'Sürüm:'; wanted=$REPLY
  [[ $wanted == auto || $wanted =~ ^[0-9]{1,3}$ ]] || continue
  pkg=$family; [[ $family == gcc && $pm == apt ]] && pkg=g++; [[ $family == gcc && $pm == fedora ]] && pkg=gcc-c++
  version=$(candidate "$pkg" || true); m=$(major "$version")
  if [[ $wanted != auto && $wanted != "$m" ]]; then
   # The distributions' packages of other versions, and the commands they install:
   # apt: g++-14 (gcc-14), clang-19 (clang-19, clang++-19); Arch: gcc14 (gcc-14, g++-14), clang19 (in /usr/lib/llvm19/bin);
   # Fedora: gcc14 and gcc14-c++ (gcc-14, g++-14), clang19 (clang-19, clang++-19).
   case $pm-$family in
   apt-gcc) pkg=g++-$wanted;; apt-clang) pkg=clang-$wanted;;
   arch-gcc) pkg=gcc$wanted;; arch-clang|fedora-clang) pkg=clang$wanted;; fedora-gcc) pkg=gcc$wanted-c++;; esac
   version=$(candidate "$pkg" || true); m=$(major "$version")
   if [[ $pm-$family == arch-clang ]]; then cc=/usr/lib/llvm$wanted/bin/clang; cxx=/usr/lib/llvm$wanted/bin/clang++
   else cc=$cc-$wanted; cxx=$cxx-$wanted; fi
  fi
  if [[ -z $m || ( $wanted != auto && $wanted != "$m" ) ]]; then say 'Requested version unavailable in configured repositories. Choose again or use an existing compiler.' 'İstenen sürüm yapılandırılmış depolarda yok. Tekrar seçin veya mevcut derleyiciyi kullanın.'; continue; fi
  compiler_pkgs=("$pkg"); if [[ $pkg == g++* ]]; then compiler_pkgs+=("${pkg/g++/gcc}"); elif [[ $pkg == gcc*-c++ ]]; then compiler_pkgs+=("${pkg%-c++}"); fi
 fi
 if [[ ! $m =~ ^[0-9]+$ ]]; then say "Could not read the compiler's version." 'Derleyicinin sürümü okunamadı.'; continue; fi
 printf '%s: %s %s\n' "$(trmsg 'Selected' 'Seçilen')" "$family" "$version"
 if [[ $family == gcc ]]; then
  if (( m < 13 )); then say 'Unsupported: GCC 13+ required.' 'Desteklenmiyor: GCC 13+ gerekli.'; continue
  elif (( m == 13 )); then say 'Limited compatibility: known internal compiler error with some coroutine lambdas.' 'Sınırlı uyumluluk: bazı coroutine lambda ifadelerinde bilinen internal compiler error.'
  elif (( m == 16 )); then say 'Tested by Kompoz; recommended.' 'Kompoz ile test edilmiş; önerilir.'
  else say 'Compatible by requirement; the GCC 13 issue is absent in GCC 14. Not every version is tested.' 'Gereksinime uygun; GCC 13 sorunu GCC 14 içinde yok. Her sürüm test edilmiş değildir.'; fi
 else
  if (( m < 17 )); then say 'Unsupported: Clang 17+ required.' 'Desteklenmiyor: Clang 17+ gerekli.'; continue
  elif (( m == 22 )); then say 'Tested by Kompoz; recommended.' 'Kompoz ile test edilmiş; önerilir.'
  else say 'Meets minimum requirements; this version is not explicitly tested.' 'Asgari gereksinimi karşılar; bu sürüm özellikle test edilmiş değildir.'; fi
 fi
 ask 'Confirm compiler selection?' 'Derleyici seçimini onaylıyor musunuz?' && break
done
packages+=("${compiler_pkgs[@]}" "${core[@]}")
while true; do
 say 'Target: 1) Linux desktop  2) Android  3) Web  4) Linux + Android + Web' 'Hedef: 1) Linux masaüstü  2) Android  3) Web  4) Linux + Android + Web'
 readval 'Choice:' 'Seçim:'; target=$REPLY; [[ $target =~ ^[1-4]$ ]] && break
done
if [[ $target != 1 ]]; then say 'The selected native compiler is for host kompozc. Android NDK / Emscripten and target libraries must be configured separately.' 'Seçilen yerel derleyici ana makinedeki kompozc içindir. Android NDK / Emscripten ve hedef kütüphaneleri ayrıca yapılandırılmalıdır.'; fi
if ask 'Install HarfBuzz (complex text shaping)?' 'HarfBuzz (karmaşık metin şekillendirme) kurulsun mu?'; then packages+=("$hb"); fi
if [[ $target == 1 || $target == 4 ]]; then
 packages+=("${vk[@]}")  # (Vulkan's headers: the GPU renderer's Vulkan backend is built with them)
 printf '%s: %s\n' "$(trmsg 'Current session' 'Mevcut oturum')" "${XDG_SESSION_TYPE:-unknown}"
 while true; do
  say 'Linux backend: 1) Wayland  2) X11  3) Both' 'Linux arka ucu: 1) Wayland  2) X11  3) Her ikisi'
  readval 'Choice:' 'Seçim:'; case $REPLY in 1) packages+=("${wl[@]}"); break;; 2) packages+=("${x11[@]}"); break;; 3) packages+=("${wl[@]}" "${x11[@]}"); break;; esac
 done
 if ask 'Install libcurl (AsyncImage downloads)?' 'libcurl (AsyncImage indirme) kurulsun mu?'; then packages+=("$curl"); fi
 if ask 'Install libwebp (WebP decoding)?' 'libwebp (WebP çözme) kurulsun mu?'; then packages+=("$webp"); fi
 if ask 'Install libturbojpeg (JPEG decoding, 3.0+ required)?' 'libturbojpeg (JPEG çözme, 3.0+ gerekli) kurulsun mu?'; then
  jv=$(candidate "$jpeg" || true); jm=$(major "$jv"); if [[ -n $jm ]] && (( jm >= 3 )); then packages+=("$jpeg"); else say 'Skipping JPEG: repository version is unavailable or below 3.0.' 'JPEG atlanıyor: depo sürümü bulunamadı veya 3.0 altında.'; fi
 fi
 if ask 'Install runtime D-Bus library (desktop theme/accent detection)?' 'D-Bus çalışma zamanı kütüphanesi (masaüstü tema/vurgu rengi) kurulsun mu?'; then packages+=("$dbus"); fi
fi
# (Both backends add xkbcommon: once is enough.)
declare -A seen; unique=(); for p in "${packages[@]}"; do [[ -n ${seen[$p]} ]] || { seen[$p]=1; unique+=("$p"); }; done; packages=("${unique[@]}")
cv=$(candidate cmake || true); cv=${cv#*:}; if [[ $cv =~ ^([0-9]+)\.([0-9]+) ]]; then if (( BASH_REMATCH[1] < 3 || (BASH_REMATCH[1] == 3 && BASH_REMATCH[2] < 24) )); then say 'Repository CMake is below 3.24; upgrade separately before building.' 'Depodaki CMake 3.24 altında; derlemeden önce ayrıca yükseltin.'; fi; fi
say 'Installation command:' 'Kurulum komutu:'
printf '%q ' "${SUDO[@]}" "${install[@]}" "${packages[@]}"; printf '\n'
if ! ask 'Proceed with installation?' 'Kurulum başlatılsın mı?'; then exit 0; fi
"${SUDO[@]}" "${install[@]}" "${packages[@]}"
say 'Installed packages. Verify toolchain:' 'Paketler kuruldu. Araç zincirini doğrulayın:'
"$cxx" --version | head -n1 || true
cmake --version | head -n1 || true
actual=$("$cxx" -dumpversion 2>/dev/null || true)
if [[ $(major "$actual") != "$m" ]]; then say 'Warning: installed compiler does not match the selected major version.' 'Uyarı: kurulu derleyici seçilen ana sürümle eşleşmiyor.'; fi
actual_cmake=$(cmake --version 2>/dev/null | awk 'NR==1 {print $3}' || true)
if [[ ! $actual_cmake =~ ^([0-9]+)\.([0-9]+) ]] || (( BASH_REMATCH[1] < 3 || (BASH_REMATCH[1] == 3 && BASH_REMATCH[2] < 24) )); then say 'Warning: CMake 3.24+ is not verified; do not build yet.' 'Uyarı: CMake 3.24+ doğrulanamadı; henüz derlemeyin.'; fi
printf 'CXX=%q\n' "$cxx"
[[ -z $cc ]] || printf 'CC=%q\n' "$cc"
say 'Pass these compiler settings to CMake on a fresh build directory.' 'Bu derleyici ayarlarını yeni bir derleme dizininde CMake komutuna aktarın.'
