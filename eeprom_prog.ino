#define CE 4
#define OE 3
#define WE 2

#define ADDR9  A1
#define ADDR10 13
#define ADDR11 A0

#define S0 A5
#define S1 A4
#define SCLK A2
#define RCLK A3

#define MODE1 1
#define MODE2 2
#define MODE4 4

#define BITAT(v, b) (((v) >> (b)) & 1)

#define CATBITS(b0, b1, b2, b3, b4, b5, b6, b7) \
  ( ((b0) << 0)                                 \
  | ((b1) << 1)                                 \
  | ((b2) << 2)                                 \
  | ((b3) << 3)                                 \
  | ((b4) << 4)                                 \
  | ((b5) << 5)                                 \
  | ((b6) << 6)                                 \
  | ((b7) << 7) )                               \

#define MIXBITS(v, b0, b1, b2, b3, b4, b5, b6, b7) \
  CATBITS(                                         \
    BITAT((v), (b0)),                              \
    BITAT((v), (b1)),                              \
    BITAT((v), (b2)),                              \
    BITAT((v), (b3)),                              \
    BITAT((v), (b4)),                              \
    BITAT((v), (b5)),                              \
    BITAT((v), (b6)),                              \
    BITAT((v), (b7))                               \
  )

uint8_t prog_chunk[4096];

int const DPINS[8] = { 5, 6, 7, 12, 11, 10, 9, 8 };

void info(char const *msg) {
  Serial.print('I');
  Serial.println(msg);
}

void error(char const *msg) {
  Serial.print('E');
  Serial.println(msg);
}

void write_data(uint8_t data) {
  for (int i = 0; i < 8; ++i) {
    pinMode(DPINS[i], OUTPUT);
    digitalWrite(DPINS[i], BITAT(data, i));
  }
}

uint8_t read_data(void) {
  output_enable(true);
  write_enable(false);

  uint8_t data = 0;
  
  for (int i = 0; i < 8; ++i) {
    pinMode(DPINS[i], INPUT);
    data |= digitalRead(DPINS[i]) << i;
  }
  
  return data;
}

void shift_bits(int b0, int b1) {
  digitalWrite(S0, b0 & 1);
  digitalWrite(S1, b1 & 1);
  delayMicroseconds(1);
  digitalWrite(SCLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(SCLK, LOW);
}

void write_addr(uint32_t addr) {
  uint8_t bits0 = MIXBITS(addr, 8, 13, 14, 12, 17, 15, 16, 18);
  uint8_t bits1 = MIXBITS(addr, 0, 1, 2, 3, 4, 5, 6, 7);

  digitalWrite(ADDR9,  BITAT(addr, 9));
  digitalWrite(ADDR10, BITAT(addr, 10));
  digitalWrite(ADDR11, BITAT(addr, 11));

  for (int i = 0; i < 8; ++i) {
    shift_bits(bits0, bits1);
    bits0 >>= 1;
    bits1 >>= 1;
  }

  delayMicroseconds(1);
  digitalWrite(RCLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(RCLK, LOW);
}

void output_enable(bool set) {
  digitalWrite(OE, set ? LOW : HIGH);
}

void write_enable(bool set) {
  digitalWrite(WE, set ? LOW : HIGH);
}

void chip_enable(bool set) {
  digitalWrite(CE, set ? LOW : HIGH);
}

void command(uint32_t addr, uint8_t data) {
  output_enable(false);
  write_enable(false);

  write_addr(addr);
  write_data(data);

  write_enable(true);
  write_enable(false);
}

bool wait_poll(int expected, int max_iter, unsigned delay = 0) {
  output_enable(true);
  write_enable(false);

  for (int i = 0; i < max_iter; ++i) {
    if ((read_data() & 128) == (expected & 128)) {
      return true;
    }

    if (delay > 0) {
      delayMicroseconds(delay);
    }
  }

  output_enable(false);

  return false;
}

int detect_device(void) {
  chip_enable(true);

  command(0x5555, 0xAA);
  command(0x2AAA, 0x55);
  command(0x5555, 0x90);

  write_addr(0x01);
  uint8_t data = read_data();

  command(0x5555, 0xAA);
  command(0x2AAA, 0x55);
  command(0x5555, 0xF0);

  switch (data) {
    case 0xB5: info("Detected device: SST39SF010"); return MODE1;
    case 0xB6: info("Detected device: SST39SF020"); return MODE2;
    case 0xB7: info("Detected device: SST39SF040"); return MODE4;
    default:   error("Unknown device!");   return -1;
  }
}

void operation_read(void) {
  int mode = detect_device();
  if (mode == -1) return;

  uint32_t nbytes = 128ul * 1024ul * mode;

  output_enable(true);

#define TXBUFSZ 128

  uint8_t buffer[TXBUFSZ];

  Serial.write(mode);
  Serial.flush();

  for (uint32_t i = 0; i < nbytes; ++i) {
    write_addr(i);

    buffer[i % TXBUFSZ] = read_data();

    if ((i % TXBUFSZ) == TXBUFSZ - 1) {
      Serial.write(buffer, TXBUFSZ);
    }
  }

  Serial.write("D");
  Serial.flush();
}

void operation_write(void) {
  int mode = detect_device();
  if (mode == -1) return;

  info("Erasing contents...");

  command(0x5555, 0xAA);
  command(0x2AAA, 0x55);
  command(0x5555, 0x80);
  command(0x5555, 0xAA);
  command(0x2AAA, 0x55);
  command(0x5555, 0x10);

  if (!wait_poll(128, 2000, 100)) {
    error("Erasure failed!");
    return;
  }

  info("Contents erased.");
  info("Flashing contents...");

  uint32_t nbytes = 128ul * 1024ul * mode;

#define RXBUFSZ 128

  uint8_t buffer[RXBUFSZ];

  Serial.write(mode);
  Serial.flush();

  for (uint32_t i = 0; i < nbytes; ++i) {
    if ((i % RXBUFSZ) == 0) {
      Serial.write("C");
      Serial.flush();

      if(Serial.readBytes(buffer, RXBUFSZ) != RXBUFSZ) {
        char msg[128];
        snprintf(msg, sizeof msg, "Receiving chunk %lu failed!", i / RXBUFSZ);
        error(msg);
        return;
      }
    }

    command(0x5555, 0xAA);
    command(0x2AAA, 0x55);
    command(0x5555, 0xA0);
    command(i, buffer[i % RXBUFSZ]);

    if (!wait_poll(buffer[i % RXBUFSZ], 100)) {
      char msg[128];
      snprintf(msg, sizeof msg, "Writing byte 0x%lx failed!", i);
      error(msg);
      return;
    }
  }

  info("Contents flashed.");
  Serial.write("D");
  Serial.flush();
}

void setup() {
  Serial.begin(115200);

  pinMode(ADDR9, OUTPUT);
  pinMode(ADDR10, OUTPUT);
  pinMode(ADDR11, OUTPUT);
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(SCLK, OUTPUT);
  pinMode(RCLK, OUTPUT);
  pinMode(CE, OUTPUT);
  pinMode(OE, OUTPUT);
  pinMode(WE, OUTPUT);

  digitalWrite(SCLK, LOW);
  digitalWrite(RCLK, LOW);

  chip_enable(false);
  output_enable(false);
  write_enable(false);
  
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  write_addr(0x10000);

  delay(10);
  int c = Serial.read();

  switch (c) {
    case 'R':
      digitalWrite(LED_BUILTIN, HIGH);
      operation_read();
      break;
    
    case 'W':
      operation_write();
      break;

    case -1:
      return;

    default:
      error("Unknown option.");
      break;
  }

  digitalWrite(LED_BUILTIN, LOW);
  chip_enable(false);
  output_enable(false);
  write_enable(false);
}
