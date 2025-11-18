# Maintainer: Your Name <youremail@domain.com>
pkgname=deskhpsdr-git
_pkgname=deskhpsdr
pkgver=0.1.r0.g$(git rev-parse --short HEAD)
pkgrel=1
pkgdesc="A GTK+/Linux based client for the HPSDR and Hermes-Lite SDR"
arch=('i686' 'x86_64' 'armv7h' 'aarch64')
url="https://github.com/dl1bz/deskhpsdr"
license=('GPL3')
depends=(
    'gtk3'
    'webkit2gtk-4.1'
    'fftw'
    'alsa-lib'
    'openssl'
    'curl'
    'libusb'
    'i2c-tools'
    'libgpiod'
    'libpulse'
    'libpcap'
    'json-c'
    'gnome-themes-extra'
    'libaio'
    'avahi'
    'libiio'
    'rtl-sdr'
    'soapysdr'
    'zstd'
    'python'
)
makedepends=('git' 'cmake' 'gcc-fortran' 'cppcheck' 'dos2unix')
optdepends=('libad9361-iio: for AD9361 support')
provides=("${_pkgname}")
conflicts=("${_pkgname}")
source=("${_pkgname}::git+${url}.git")
sha256sums=('SKIP')

pkgver() {
  cd "${_pkgname}"
  git describe --long --tags | sed 's/\([^-]*-g\)/r\1/;s/-/./g'
}

prepare() {
  cd "${srcdir}/${_pkgname}"
  sed -i 's/CFLAGS?=/CFLAGS+=/' wdsp-1.28/Makefile
}

build() {
  cd "${srcdir}/${_pkgname}"

  # Create the make.config.deskhpsdr file
  cat > make.config.deskhpsdr <<-EOF
		TCI=ON
		GPIO=OFF
		MIDI=ON
		SATURN=OFF
		USBOZY=OFF
		SOAPYSDR=ON
		STEMLAB=OFF
		EXTENDED_NR=OFF
		TTS=ON
		AUDIO=PULSE
		ATU=OFF
		COPYMODE=OFF
		AUTOGAIN=OFF
		REGION1=OFF
		WMAP=OFF
		EQ12=OFF
		DEVEL=OFF
		TAHOEFIX=ON
	EOF

  make
}

package() {
  cd "${srcdir}/${_pkgname}"
  install -Dm755 "${_pkgname}" "${pkgdir}/usr/bin/${_pkgname}"
  install -Dm644 "release/${_pkgname}/hpsdr_icon.png" "${pkgdir}/usr/share/icons/hicolor/256x256/apps/deskhpsdr.png"
  install -Dm644 "LINUX/deskHPSDR.desktop" "${pkgdir}/usr/share/applications/deskhpsdr.desktop"
}
