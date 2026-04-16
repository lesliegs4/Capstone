#include <avr/io.h>
#include <util/delay.h>
#include <util/twi.h>
#include <stdint.h>
#include <stdbool.h>

#include "serial.h"

#ifndef F_CPU
#error "F_CPU must be defined (e.g. via -DF_CPU=...)"
#endif

// --- User-configurable ---
#ifndef I2C_SCL_HZ
#define I2C_SCL_HZ 100000UL
#endif

// AS7262 7-bit I2C address (from Adafruit guide)
#define AS7262_ADDR_7BIT 0x49

// AS726x virtual register map (from Adafruit CircuitPython driver)
#define AS726X_HW_VERSION     0x00
#define AS726X_CONTROL_SETUP  0x04
#define AS726X_INT_T          0x05
#define AS726X_DEVICE_TEMP    0x06
#define AS726X_LED_CONTROL    0x07

#define AS7262_V_HIGH 0x08
#define AS7262_V_LOW  0x09
#define AS7262_B_HIGH 0x0A
#define AS7262_B_LOW  0x0B
#define AS7262_G_HIGH 0x0C
#define AS7262_G_LOW  0x0D
#define AS7262_Y_HIGH 0x0E
#define AS7262_Y_LOW  0x0F
#define AS7262_O_HIGH 0x10
#define AS7262_O_LOW  0x11
#define AS7262_R_HIGH 0x12
#define AS7262_R_LOW  0x13

// AS726x hardware registers used to access virtual regs over I2C
#define AS726X_SLAVE_STATUS_REG 0x00
#define AS726X_SLAVE_WRITE_REG  0x01
#define AS726X_SLAVE_READ_REG   0x02

// Status bits (from Adafruit driver)
#define AS726X_SLAVE_TX_VALID 0x02
#define AS726X_SLAVE_RX_VALID 0x01

// CONTROL_SETUP mode bits [3:2]
#define AS726X_MODE_0     0x00
#define AS726X_MODE_1     0x01
#define AS726X_MODE_2     0x02
#define AS726X_ONE_SHOT   0x03

// CONTROL_SETUP data-ready bit [1]
#define AS726X_DATA_RDY_MASK (1 << 1)

// --- AS7262 LED configuration ---
// Override via CFLAGS, e.g.:
//   make as7262_test.hex CFLAGS='-DAS7262_USE_DRV_LED=1 -DAS7262_DRV_LED_CURRENT_IDX=3'
#ifndef AS7262_USE_DRV_LED
#define AS7262_USE_DRV_LED 1
#endif

// 0=12.5mA, 1=25mA, 2=50mA, 3=100mA
#ifndef AS7262_DRV_LED_CURRENT_IDX
#define AS7262_DRV_LED_CURRENT_IDX 3
#endif

#ifndef AS7262_AMBIENT_SUBTRACT
#define AS7262_AMBIENT_SUBTRACT 1
#endif

// --- Small serial helpers ---
static void serial_out_str(const char *s)
{
    while (*s) serial_out(*s++);
}

static void serial_out_u8_hex(uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    serial_out(hex[(v >> 4) & 0x0F]);
    serial_out(hex[v & 0x0F]);
}

static void serial_out_u16_dec(uint16_t v)
{
    char buf[6];
    uint8_t i = 0;
    if (v == 0) {
        serial_out('0');
        return;
    }
    while (v > 0 && i < (uint8_t)(sizeof(buf) - 1)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) serial_out(buf[--i]);
}

static void serial_out_i16_dec(int16_t v)
{
    if (v < 0) {
        serial_out('-');
        v = -v;
    }
    serial_out_u16_dec((uint16_t)v);
}

static void delay_ms(uint16_t ms)
{
    while (ms--) _delay_ms(1);
}

// --- Color reference matching (test-strip chart) ---
#ifndef REF_MATCH_MAX_DIST
// Euclidean RGB distance threshold (0-441). Lower = stricter match.
#define REF_MATCH_MAX_DIST 55U
#endif

typedef struct {
    const char *name;
    uint8_t lo_r, lo_g, lo_b;
    uint8_t hi_r, hi_g, hi_b;   // if same as lo => point color
    uint8_t is_range;           // 1 => lo..hi gradient range, 0 => point
    int16_t val_lo_x10;         // value * 10 (for pH-like decimals); ignored if point-only
    int16_t val_hi_x10;
    const char *units;
} color_ref_t;

static const color_ref_t SAFE_REFS[] = {
    // Saltwater ok range, nitrate row 1 (20-40): #f1cab5 - #e47e83
    { "Saltwater nitrate row1 OK (20-40)", 0xF1,0xCA,0xB5, 0xE4,0x7E,0x83, 1, 200, 400, "ppm" },
    // Saltwater nitrate row 2 (1.0-3.0): #e6daa7
    { "Saltwater nitrate row2 (1.0-3.0)", 0xE6,0xDA,0xA7, 0xE6,0xDA,0xA7, 0,  10,  30, "ppm" },
    // Total alkalinity (180-300): #445542 - #424d48
    { "Saltwater total alkalinity (180-300)", 0x44,0x55,0x42, 0x42,0x4D,0x48, 1, 1800, 3000, "ppm" },
    // pH (7.8-8.4): #c13233 - #bf0d44
    { "Saltwater pH (7.8-8.4)", 0xC1,0x32,0x33, 0xBF,0x0D,0x44, 1,  78,  84, "pH" },
};

static uint32_t u8_sq_diff(uint8_t a, uint8_t b)
{
    int16_t d = (int16_t)a - (int16_t)b;
    return (uint32_t)(d * d);
}

static uint32_t rgb_dist2_point(uint8_t pr, uint8_t pg, uint8_t pb,
                                uint8_t r, uint8_t g, uint8_t b)
{
    return u8_sq_diff(pr, r) + u8_sq_diff(pg, g) + u8_sq_diff(pb, b);
}

// Distance from point P to segment AB in RGB space, plus t (0..65535) along segment.
static uint32_t rgb_dist2_segment(uint8_t pr, uint8_t pg, uint8_t pb,
                                  uint8_t ar, uint8_t ag, uint8_t ab,
                                  uint8_t br, uint8_t bg, uint8_t bb,
                                  uint16_t *t_q16_out)
{
    int32_t vx = (int32_t)br - (int32_t)ar;
    int32_t vy = (int32_t)bg - (int32_t)ag;
    int32_t vz = (int32_t)bb - (int32_t)ab;

    int32_t wx = (int32_t)pr - (int32_t)ar;
    int32_t wy = (int32_t)pg - (int32_t)ag;
    int32_t wz = (int32_t)pb - (int32_t)ab;

    int32_t c2 = vx*vx + vy*vy + vz*vz;
    if (c2 <= 0) {
        if (t_q16_out) *t_q16_out = 0;
        return rgb_dist2_point(pr, pg, pb, ar, ag, ab);
    }

    int32_t c1 = wx*vx + wy*vy + wz*vz;
    int32_t t_q16 = (int32_t)(((int64_t)c1 << 16) / (int64_t)c2);
    if (t_q16 < 0) t_q16 = 0;
    if (t_q16 > 65535) t_q16 = 65535;
    if (t_q16_out) *t_q16_out = (uint16_t)t_q16;

    int32_t nx = (int32_t)ar + (int32_t)(((int64_t)vx * t_q16) >> 16);
    int32_t ny = (int32_t)ag + (int32_t)(((int64_t)vy * t_q16) >> 16);
    int32_t nz = (int32_t)ab + (int32_t)(((int64_t)vz * t_q16) >> 16);

    int32_t dx = (int32_t)pr - nx;
    int32_t dy = (int32_t)pg - ny;
    int32_t dz = (int32_t)pb - nz;
    return (uint32_t)(dx*dx + dy*dy + dz*dz);
}

static void serial_out_x10_value(int16_t v_x10)
{
    if (v_x10 < 0) {
        serial_out('-');
        v_x10 = -v_x10;
    }
    uint16_t whole = (uint16_t)(v_x10 / 10);
    uint8_t frac = (uint8_t)(v_x10 % 10);
    serial_out_u16_dec(whole);
    serial_out('.');
    serial_out((char)('0' + frac));
}

static uint8_t u32_to_u8_scaled(uint32_t v, uint32_t vmax)
{
    if (vmax == 0) return 0;
    uint32_t x = v * 255UL;
    x /= vmax;
    if (x > 255UL) x = 255UL;
    return (uint8_t)x;
}

// --- AVR TWI (I2C) ---
#ifndef TWI_TIMEOUT_ITER
#define TWI_TIMEOUT_ITER 60000UL
#endif

static bool twi_wait_twint(void)
{
    uint32_t n = TWI_TIMEOUT_ITER;
    while (!(TWCR & (1 << TWINT))) {
        if (--n == 0) return false;
    }
    return true;
}

static void twi_init(void)
{
    // Prescaler = 1
    TWSR = 0x00;

    // SCL = F_CPU / (16 + 2*TWBR*prescaler)
    // TWBR = ((F_CPU / SCL) - 16) / 2
    uint32_t twbr = ((F_CPU / I2C_SCL_HZ) - 16UL) / 2UL;
    if (twbr > 255UL) twbr = 255UL;
    TWBR = (uint8_t)twbr;

    TWCR = (1 << TWEN);
}

static bool twi_start(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    if (!twi_wait_twint()) return false;

    uint8_t st = TW_STATUS;
    return (st == TW_START) || (st == TW_REP_START);
}

static void twi_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

static bool twi_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    if (!twi_wait_twint()) return false;

    uint8_t st = TW_STATUS;
    return (st == TW_MT_SLA_ACK) || (st == TW_MT_DATA_ACK) ||
           (st == TW_MR_SLA_ACK);
}

static uint8_t twi_read(bool ack)
{
    if (ack) TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    else     TWCR = (1 << TWINT) | (1 << TWEN);
    (void)twi_wait_twint();
    return TWDR;
}

static bool i2c_write_u8(uint8_t addr7, uint8_t reg, uint8_t val)
{
    if (!twi_start()) return false;
    if (!twi_write((uint8_t)((addr7 << 1) | 0))) { twi_stop(); return false; }
    if (!twi_write(reg)) { twi_stop(); return false; }
    if (!twi_write(val)) { twi_stop(); return false; }
    twi_stop();
    return true;
}

static bool i2c_read_u8(uint8_t addr7, uint8_t reg, uint8_t *out)
{
    if (!twi_start()) return false;
    if (!twi_write((uint8_t)((addr7 << 1) | 0))) { twi_stop(); return false; }
    if (!twi_write(reg)) { twi_stop(); return false; }

    if (!twi_start()) { twi_stop(); return false; }
    if (!twi_write((uint8_t)((addr7 << 1) | 1))) { twi_stop(); return false; }

    *out = twi_read(false);
    twi_stop();
    return true;
}

// --- AS7262 "virtual register" I2C protocol ---
static bool as726x_hw_read_u8(uint8_t reg, uint8_t *out)
{
    return i2c_read_u8(AS7262_ADDR_7BIT, reg, out);
}

static bool as726x_hw_write_u8(uint8_t reg, uint8_t val)
{
    return i2c_write_u8(AS7262_ADDR_7BIT, reg, val);
}

static bool as726x_virtual_read_u8(uint8_t vreg, uint8_t *out)
{
    // Wait until there is no pending TX for the slave.
    while (1) {
        uint8_t st;
        if (!as726x_hw_read_u8(AS726X_SLAVE_STATUS_REG, &st)) return false;
        if ((st & AS726X_SLAVE_TX_VALID) == 0) break;
    }

    // Request a read of vreg.
    if (!as726x_hw_write_u8(AS726X_SLAVE_WRITE_REG, vreg)) return false;

    // Wait for RX_VALID, then read from SLAVE_READ_REG.
    while (1) {
        uint8_t st;
        if (!as726x_hw_read_u8(AS726X_SLAVE_STATUS_REG, &st)) return false;
        if ((st & AS726X_SLAVE_RX_VALID) != 0) break;
    }

    return as726x_hw_read_u8(AS726X_SLAVE_READ_REG, out);
}

static bool as726x_virtual_write_u8(uint8_t vreg, uint8_t val)
{
    // Wait until there is no pending TX for the slave.
    while (1) {
        uint8_t st;
        if (!as726x_hw_read_u8(AS726X_SLAVE_STATUS_REG, &st)) return false;
        if ((st & AS726X_SLAVE_TX_VALID) == 0) break;
    }

    // Send vreg address with bit7 set (write).
    if (!as726x_hw_write_u8(AS726X_SLAVE_WRITE_REG, (uint8_t)(vreg | 0x80))) return false;

    // Wait until write buffer ready, then write the value.
    while (1) {
        uint8_t st;
        if (!as726x_hw_read_u8(AS726X_SLAVE_STATUS_REG, &st)) return false;
        if ((st & AS726X_SLAVE_TX_VALID) == 0) break;
    }

    return as726x_hw_write_u8(AS726X_SLAVE_WRITE_REG, val);
}

static bool as7262_read_u16(uint8_t high_reg, uint8_t low_reg, uint16_t *out)
{
    uint8_t hi, lo;
    if (!as726x_virtual_read_u8(high_reg, &hi)) return false;
    if (!as726x_virtual_read_u8(low_reg, &lo)) return false;
    *out = ((uint16_t)hi << 8) | lo;
    return true;
}

static bool as7262_set_integration_time_ms(uint16_t ms)
{
    // INT_T is in units of 2.8ms. Compute floor(ms / 2.8) without floats:
    // ms / 2.8 = ms * 10 / 28
    uint16_t it = (uint16_t)((uint32_t)ms * 10UL / 28UL);
    if (it > 255U) it = 255U;
    return as726x_virtual_write_u8(AS726X_INT_T, (uint8_t)it);
}

static bool as7262_set_gain_64x(void)
{
    uint8_t st;
    if (!as726x_virtual_read_u8(AS726X_CONTROL_SETUP, &st)) return false;
    st &= (uint8_t)~(0x3U << 4);
    st |= (uint8_t)(0x3U << 4); // gain index 3 => 64x
    return as726x_virtual_write_u8(AS726X_CONTROL_SETUP, st);
}

static bool as7262_set_mode(uint8_t mode)
{
    uint8_t st;
    if (!as726x_virtual_read_u8(AS726X_CONTROL_SETUP, &st)) return false;
    st &= (uint8_t)~(0x3U << 2);
    st |= (uint8_t)((mode & 0x3U) << 2);
    return as726x_virtual_write_u8(AS726X_CONTROL_SETUP, st);
}

static bool as7262_configure_driver_led(bool on, uint8_t current_idx)
{
    uint8_t st;
    if (!as726x_virtual_read_u8(AS726X_LED_CONTROL, &st)) return false;

    st &= (uint8_t)~(1U << 3);        // driver enable bit
    st &= (uint8_t)~(0x3U << 4);      // driver current bits
    st |= (uint8_t)((current_idx & 0x3U) << 4);
    if (on) st |= (1U << 3);

    return as726x_virtual_write_u8(AS726X_LED_CONTROL, st);
}

static bool as7262_start_one_shot(void)
{
    // Clear the "data ready" latch bit (bit1) by writing CONTROL_SETUP with it cleared,
    // then set ONE_SHOT mode. This mirrors the Adafruit driver intent.
    uint8_t st;
    if (!as726x_virtual_read_u8(AS726X_CONTROL_SETUP, &st)) return false;
    st &= (uint8_t)~AS726X_DATA_RDY_MASK;
    if (!as726x_virtual_write_u8(AS726X_CONTROL_SETUP, st)) return false;
    return as7262_set_mode(AS726X_ONE_SHOT);
}

static bool as7262_data_ready(bool *ready)
{
    uint8_t st;
    if (!as726x_virtual_read_u8(AS726X_CONTROL_SETUP, &st)) return false;
    *ready = ((st & AS726X_DATA_RDY_MASK) != 0);
    return true;
}

static bool as7262_init(void)
{
    // Reset device by writing bit7 of CONTROL_SETUP.
    if (!as726x_virtual_write_u8(AS726X_CONTROL_SETUP, 0x80)) return false;
    delay_ms(1000);

    uint8_t version = 0;
    if (!as726x_virtual_read_u8(AS726X_HW_VERSION, &version)) return false;
    if (version != 0x40) return false;

    if (!as7262_set_integration_time_ms(140)) return false;
    if (!as7262_set_gain_64x()) return false;
    if (!as7262_set_mode(AS726X_MODE_2)) return false; // continuous all channels by default

#if AS7262_USE_DRV_LED
    if (!as7262_configure_driver_led(true, (uint8_t)AS7262_DRV_LED_CURRENT_IDX)) return false;
#else
    (void)as7262_configure_driver_led(false, 0);
#endif

    return true;
}

static bool as7262_measure_raw(uint8_t *temp_c,
                               uint16_t *v, uint16_t *b, uint16_t *g,
                               uint16_t *y, uint16_t *o, uint16_t *r)
{
    if (!as7262_start_one_shot()) return false;

    bool ready = false;
    for (uint16_t tries = 0; tries < 200; tries++) { // ~2s max
        if (!as7262_data_ready(&ready)) { ready = false; break; }
        if (ready) break;
        delay_ms(10);
    }
    if (!ready) return false;

    if (!as726x_virtual_read_u8(AS726X_DEVICE_TEMP, temp_c)) return false;
    if (!as7262_read_u16(AS7262_V_HIGH, AS7262_V_LOW, v)) return false;
    if (!as7262_read_u16(AS7262_B_HIGH, AS7262_B_LOW, b)) return false;
    if (!as7262_read_u16(AS7262_G_HIGH, AS7262_G_LOW, g)) return false;
    if (!as7262_read_u16(AS7262_Y_HIGH, AS7262_Y_LOW, y)) return false;
    if (!as7262_read_u16(AS7262_O_HIGH, AS7262_O_LOW, o)) return false;
    if (!as7262_read_u16(AS7262_R_HIGH, AS7262_R_LOW, r)) return false;

    return true;
}

int main(void)
{
    serial_init();
    twi_init();

    serial_out_str("\r\nAS7262 TEST (I2C on PC4=SDA, PC5=SCL)\r\n");
    serial_out_str("Addr 0x");
    serial_out_u8_hex((uint8_t)AS7262_ADDR_7BIT);
    serial_out_str("\r\n");

    if (!as7262_init()) {
        serial_out_str("ERROR: AS7262 init failed (check wiring, power, pullups)\r\n");
        while (1) { delay_ms(1000); }
    }

    serial_out_str("AS7262 OK\r\n");

    while (1) {
        uint8_t temp_c = 0;
        uint16_t v, b, g, y, o, r;

#if AS7262_USE_DRV_LED && AS7262_AMBIENT_SUBTRACT
        uint8_t temp_ambient = 0, temp_lit = 0;
        uint16_t v0, b0, g0, y0, o0, r0;
        uint16_t v1, b1, g1, y1, o1, r1;

        if (!as7262_configure_driver_led(false, (uint8_t)AS7262_DRV_LED_CURRENT_IDX)) {
            serial_out_str("ERROR: LED off\r\n");
            delay_ms(500);
            continue;
        }
        delay_ms(10);
        if (!as7262_measure_raw(&temp_ambient, &v0, &b0, &g0, &y0, &o0, &r0)) {
            serial_out_str("ERROR: ambient sample\r\n");
            delay_ms(500);
            continue;
        }

        if (!as7262_configure_driver_led(true, (uint8_t)AS7262_DRV_LED_CURRENT_IDX)) {
            serial_out_str("ERROR: LED on\r\n");
            delay_ms(500);
            continue;
        }
        delay_ms(10);
        if (!as7262_measure_raw(&temp_lit, &v1, &b1, &g1, &y1, &o1, &r1)) {
            serial_out_str("ERROR: lit sample\r\n");
            delay_ms(500);
            continue;
        }

        temp_c = temp_lit;
        v = (v1 > v0) ? (uint16_t)(v1 - v0) : 0;
        b = (b1 > b0) ? (uint16_t)(b1 - b0) : 0;
        g = (g1 > g0) ? (uint16_t)(g1 - g0) : 0;
        y = (y1 > y0) ? (uint16_t)(y1 - y0) : 0;
        o = (o1 > o0) ? (uint16_t)(o1 - o0) : 0;
        r = (r1 > r0) ? (uint16_t)(r1 - r0) : 0;
#else
        if (!as7262_measure_raw(&temp_c, &v, &b, &g, &y, &o, &r)) {
            serial_out_str("ERROR: sample\r\n");
            delay_ms(500);
            continue;
        }
#endif

        serial_out_str("T=");
        serial_out_i16_dec((int16_t)temp_c);
        serial_out_str("C ");

        serial_out_str("V=");
        serial_out_u16_dec(v);
        serial_out_str(" B=");
        serial_out_u16_dec(b);
        serial_out_str(" G=");
        serial_out_u16_dec(g);
        serial_out_str(" Y=");
        serial_out_u16_dec(y);
        serial_out_str(" O=");
        serial_out_u16_dec(o);
        serial_out_str(" R=");
        serial_out_u16_dec(r);

        // Simple 6-band → RGB approximation:
        // - boost blue with violet, boost green with yellow, boost red with orange
        uint32_t r_mix = (uint32_t)r + ((uint32_t)o >> 1);
        uint32_t g_mix = (uint32_t)g + ((uint32_t)y >> 1);
        uint32_t b_mix = (uint32_t)b + ((uint32_t)v >> 1);

        uint32_t max_rgb = r_mix;
        if (g_mix > max_rgb) max_rgb = g_mix;
        if (b_mix > max_rgb) max_rgb = b_mix;

        uint8_t r8 = u32_to_u8_scaled(r_mix, max_rgb);
        uint8_t g8 = u32_to_u8_scaled(g_mix, max_rgb);
        uint8_t b8 = u32_to_u8_scaled(b_mix, max_rgb);

        serial_out_str(" HEX=#");
        serial_out_u8_hex(r8);
        serial_out_u8_hex(g8);
        serial_out_u8_hex(b8);

        // Match against provided "safe" reference colors.
        uint32_t best_d2 = 0xFFFFFFFFUL;
        uint16_t best_t = 0;
        const color_ref_t *best = 0;

        for (uint8_t i = 0; i < (uint8_t)(sizeof(SAFE_REFS) / sizeof(SAFE_REFS[0])); i++) {
            const color_ref_t *ref = &SAFE_REFS[i];
            uint32_t d2;
            uint16_t t = 0;
            if (ref->is_range) {
                d2 = rgb_dist2_segment(r8, g8, b8,
                                       ref->lo_r, ref->lo_g, ref->lo_b,
                                       ref->hi_r, ref->hi_g, ref->hi_b,
                                       &t);
            } else {
                d2 = rgb_dist2_point(r8, g8, b8, ref->lo_r, ref->lo_g, ref->lo_b);
            }
            if (d2 < best_d2) {
                best_d2 = d2;
                best_t = t;
                best = ref;
            }
        }

        uint32_t thr2 = (uint32_t)REF_MATCH_MAX_DIST * (uint32_t)REF_MATCH_MAX_DIST;
        if (best) {
            serial_out_str(" REF=");
            serial_out_str(best->name);

            if (best_d2 <= thr2) {
                serial_out_str(" (MATCH)");
            } else {
                serial_out_str(" (NEAR)");
            }

            // If it's a gradient reference, emit an estimated value along the gradient.
            if (best->is_range) {
                int32_t span = (int32_t)best->val_hi_x10 - (int32_t)best->val_lo_x10;
                int32_t est = (int32_t)best->val_lo_x10 + (int32_t)(((int64_t)span * best_t) >> 16);
                serial_out_str(" est=");
                serial_out_x10_value((int16_t)est);
                serial_out(' ');
                serial_out_str(best->units);
            }
        }

        serial_out_str("\r\n");

        delay_ms(500);
    }
}

