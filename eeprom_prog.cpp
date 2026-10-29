#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string_view>
#include <termios.h>
#include <unistd.h>
#include <vector>

static int fd = 0;

static bool read(char &c) {
  int n, tries = 0;
  while (tries++ < 30) {
    n = read(fd, &c, 1);
    if (n != 0) {
      return n > 0;
    }
  }

  return false;
}

static int readnext(void) {
  char c;
  bool err = false;

  if (!read(c)) {
    goto err;
  }

  if (c == 'E' || c == 'I') {
    if (c == 'E') {
      std::cout << "ERROR  ";
      err = true;
    } else {
      std::cout << "INFO   ";
    }

    while (true) {
      if (!read(c)) {
        goto err;
      }

      std::cout << c;

      if (c == '\n') {
        std::cout << std::flush;

        if (err) {
          close(fd);
          std::exit(EXIT_FAILURE);
        }

        return readnext();
      }
    }
  }

  return c;

err:
  std::cerr << "Failed to read from serial." << std::endl;
  close(fd);
  std::exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {

  if (argc != 4) {
  usage:
    std::cerr << "Usage: " << *argv << " [read|write] [device] [file]"
              << std::endl;
  fail:
    close(fd);
    return EXIT_FAILURE;
  }

  std::string_view mode(argv[1]);

  fd = open(argv[2], O_RDWR | O_NOCTTY | O_SYNC);
  if (fd < 0) {
    std::cerr << "Failed to open device." << std::endl;
    goto fail;
  }

  termios tty{};
  if (tcgetattr(fd, &tty) != 0) {
    std::cerr << "tcgetattr failed." << std::endl;
    goto fail;
  }

  cfsetospeed(&tty, B115200);
  cfsetispeed(&tty, B115200);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
  tty.c_iflag = 0;
  tty.c_oflag = 0;
  tty.c_lflag = 0;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 10;

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    std::cerr << "tcsetattr failed." << std::endl;
    goto fail;
  }

  sleep(2);
  tcflush(fd, TCIFLUSH);

  if (mode == "read") {
    std::ofstream f(argv[3],
                    std::ios::out | std::ios::trunc | std::ios::binary);

    if (!f) {
      std::cerr << "Failed to open file." << std::endl;
      goto fail;
    }

    write(fd, "R", 1);
    tcdrain(fd);

    size_t sz = 128 * 1024;

    int c = readnext();
    switch (c) {
    case 1:
    case 2:
    case 4:
      sz *= c;
      break;

    default:
      std::cerr << "Received invalid device type." << std::endl;
      goto fail;
    }

    while (sz-- > 0) {
      char c;
      if (!read(c)) {
        std::cerr << "Failed to read from serial." << std::endl;
        goto fail;
      }

      if ((sz % 1024) == 0) {
        std::cout << sz << " bytes remaining." << std::endl;
      }

      f.write(&c, 1);
    }

    f.close();

    switch (readnext()) {
    case 'D':
      std::cout << "Done." << std::endl;
      close(fd);
      return EXIT_SUCCESS;

    default:
      std::cout << "Expected end-of-flash, but received data instead."
                << std::endl;
      goto fail;
    }

  } else if (mode == "write") {
    std::ifstream f(argv[3], std::ios::in | std::ios::binary);

    if (!f) {
      std::cerr << "Failed to open file." << std::endl;
      goto fail;
    }

    write(fd, "W", 1);
    tcdrain(fd);

    size_t sz = 128 * 1024;

    int c = readnext();
    switch (c) {
    case 1:
    case 2:
    case 4:
      sz *= c;
      break;

    default:
      std::cerr << "Received invalid device type." << std::endl;
      goto fail;
    }

    std::vector<char> data;
    data.resize(sz);

    f.seekg(0, std::ios::end);

    if (f.tellg() != sz) {
      std::cerr << "Unexpected file size (expected " << sz << " bytes, got "
                << f.tellg() << " instead). " << std::endl;
      goto fail;
    }

    f.seekg(0, std::ios::beg);
    f.read(data.data(), sz);

    if (f.fail() || f.gcount() != sz) {
      std::cerr
          << "Failed to read required amount of bytes from file (expected "
          << sz << ", got " << f.gcount() << ")." << std::endl;
      goto fail;
    }

    f.close();

    size_t off = 0;

    while (true) {
      switch (readnext()) {
      case 'D':
        std::cout << "Done." << std::endl;
        close(fd);
        return EXIT_SUCCESS;

      case 'C':
        if (off >= sz) {
          std::cerr << "Failed to write: programmer requested more bytes than "
                       "expected."
                    << std::endl;
          goto fail;
        }

        std::cout << (sz - off) << " bytes remaining." << std::endl;

        write(fd, data.data() + off, 128);
        tcdrain(fd);

        off += 128;
        break;
      }
    }
  }

  goto usage;
}
