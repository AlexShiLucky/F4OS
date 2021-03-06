/*
 * Copyright (C) 2013, 2014 F4OS Authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <libfdt.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arch/system.h>
#include <arch/chip/gpio.h>
#include <arch/chip/rcc.h>
#include <arch/chip/spi.h>
#include <dev/device.h>
#include <dev/fdtparse.h>
#include <dev/raw_mem.h>
#include <dev/hw/gpio.h>
#include <dev/hw/spi.h>
#include <kernel/mutex.h>
#include <kernel/class.h>
#include <kernel/init.h>
#include <mm/mm.h>

#define STM32F4_SPI_COMPAT "stmicro,stm32f407-spi"

struct stm32f4_spi {
    uint8_t                 ready;
    int                     periph_id;
    long                    periph_clock;
    struct gpio             *gpio[3];    /* Each SPI port uses 3 GPIOs */
    struct stm32f4_spi_regs *regs;
};

static long get_clock(struct stm32f4_spi *port) {
    switch (raw_mem_read(&port->regs->CR1) & SPI_CR1_BR_MASK) {
    case SPI_CR1_BR_2:
        return port->periph_clock / 2;
    case SPI_CR1_BR_4:
        return port->periph_clock / 4;
    case SPI_CR1_BR_8:
        return port->periph_clock / 8;
    case SPI_CR1_BR_16:
        return port->periph_clock / 16;
    case SPI_CR1_BR_32:
        return port->periph_clock / 32;
    case SPI_CR1_BR_64:
        return port->periph_clock / 64;
    case SPI_CR1_BR_128:
        return port->periph_clock / 128;
    case SPI_CR1_BR_256:
        return port->periph_clock / 256;
    default:
        return -1;
    }
}

static long set_clock(struct stm32f4_spi *port, long desired_clock) {
    int divisor = DIV_ROUND_UP(port->periph_clock, desired_clock);
    uint32_t setting;

    if (divisor <= 2) {
        setting = SPI_CR1_BR_2;
    }
    else if (divisor <= 4) {
        setting = SPI_CR1_BR_4;
    }
    else if (divisor <= 8) {
        setting = SPI_CR1_BR_8;
    }
    else if (divisor <= 16) {
        setting = SPI_CR1_BR_16;
    }
    else if (divisor <= 32) {
        setting = SPI_CR1_BR_32;
    }
    else if (divisor <= 64) {
        setting = SPI_CR1_BR_64;
    }
    else if (divisor <= 128) {
        setting = SPI_CR1_BR_128;
    }
    else {
        setting = SPI_CR1_BR_256;
    }

    raw_mem_set_mask(&port->regs->CR1, SPI_CR1_BR_MASK, setting);

    return get_clock(port);
}

/* The SPI mutex must already be held when calling this function */
static int stm32f4_spi_initialize(struct spi *s) {
    int ret = 0;
    struct stm32f4_spi *port = (struct stm32f4_spi *) s->priv;

    /* Already initialized? */
    if (port->ready) {
        goto out;
    }

    /* Enable SPI Clock */
    ret = rcc_set_clock_enable(port->periph_id, 1);
    if (ret) {
        goto out;
    }

    /* Clock high on idle, Capture on rising edge, 16-bit data format */
    raw_mem_set_bits(&port->regs->CR1,
                     SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI);

    /* Default clock = 1MHz */
    set_clock(port, 1 * 1000 * 1000);

    raw_mem_set_bits(&port->regs->CR1, SPI_CR1_SPE);

    port->ready = 1;

out:
    return ret;
}

static int stm32f4_spi_init(struct spi *s) {
    int ret;

    acquire(&s->lock);
    ret = stm32f4_spi_initialize(s);
    release(&s->lock);

    return ret;
}

static int stm32f4_spi_deinit(struct spi *s) {
    /* Turn off clocks? */
    /* Release GPIOs? */

    return 0;
}

static long stm32f4_spi_get_clock(struct spi *spi) {
    struct stm32f4_spi *port;
    long ret;

    if (!spi) {
        return -1;
    }

    port = spi->priv;

    acquire(&spi->lock);

    if (!port->ready) {
        ret = stm32f4_spi_initialize(spi);
        if (ret) {
            goto out;
        }
    }

    ret = get_clock(port);

out:
    release(&spi->lock);
    return ret;
}

static long stm32f4_spi_set_clock(struct spi *spi, long clock) {
    struct stm32f4_spi *port;
    long ret;

    if (!spi) {
        return -1;
    }

    port = spi->priv;

    acquire(&spi->lock);

    if (!port->ready) {
        ret = stm32f4_spi_initialize(spi);
        if (ret) {
            goto out;
        }
    }

    ret = set_clock(port, clock);

out:
    release(&spi->lock);
    return ret;
}

static int stm32f4_spi_send_receive(struct spi *spi, uint8_t send,
                                    uint8_t *receive) {
    uint8_t *data;
    uint8_t null;
    int count;
    struct stm32f4_spi *port = (struct stm32f4_spi *) spi->priv;

    /* Provide a black hole to write to if receive is NULL */
    if (receive) {
        data = receive;
    }
    else {
        data = &null;
    }

    /* Transmit data */
    count = 10000;
    while (!(raw_mem_read(&port->regs->SR) & SPI_SR_TXNE)) {
        if (!count--) {
            return -1;
        }
    }
    raw_mem_write(&port->regs->DR, send);

    /* Wait for response
     * Note: this "response" was transmitted while we were
     * transmitting the data above, it is not the device's response to that request. */
    count = 10000;
    while (!(raw_mem_read(&port->regs->SR) & SPI_SR_RXNE)) {
        if (!count--) {
            return -1;
        }
    }
    *data = raw_mem_read(&port->regs->DR);

    return 0;
}

static int stm32f4_spi_read_write(struct spi *spi, struct spi_dev *dev,
                                  uint8_t *read_data, uint8_t *write_data,
                                  uint32_t num) {
    /* Verify valid SPI */
    if (!spi) {
        return -1;
    }

    struct stm32f4_spi *port = (struct stm32f4_spi *) spi->priv;

    if (!port) {
        return -1;
    }

    /* Verify valid SPI device */
    if (!dev || !dev->cs) {
        return -1;
    }

    if (num == 0) {
        return 0;
    }

    struct gpio_ops *cs_ops = (struct gpio_ops *) dev->cs->obj.ops;
    uint32_t total = 0;
    int ret;

    if (!dev->extended_transaction) {
        acquire(&spi->lock);
        cs_ops->set_output_value(dev->cs, 0);
    }

    /* Initialized? */
    if (!port->ready) {
        ret = stm32f4_spi_initialize(spi);
        if (ret) {
            goto out;
        }
    }

    /* Data MUST be read after each TX */

    /* Clear overrun by reading old data */
    if (raw_mem_read(&port->regs->SR) & SPI_SR_OVR) {
        READ_AND_DISCARD(&port->regs->DR);
        READ_AND_DISCARD(&port->regs->SR);
    }

    while (num--) {
        /* Handle NULL read_data and write_data */
        uint8_t send = write_data ? *write_data++ : 0;
        uint8_t *receive = read_data ? read_data++ : NULL;

        /* Transmit data */
        if (stm32f4_spi_send_receive(spi, send, receive)) {
            ret = -1;
            goto out;
        }

        total += 1;
    }

    ret = total;

out:
    if (!dev->extended_transaction) {
        cs_ops->set_output_value(dev->cs, 1);
        release(&spi->lock);
    }

    return ret;
}

static int stm32f4_spi_write(struct spi *spi, struct spi_dev *dev,
                             uint8_t *data, uint32_t num) {
    return stm32f4_spi_read_write(spi, dev, NULL, data, num);
}

static int stm32f4_spi_read(struct spi *spi, struct spi_dev *dev,
                            uint8_t *data, uint32_t num) {
    return stm32f4_spi_read_write(spi, dev, data, NULL, num);
}

static void stm32f4_spi_start_transaction(struct spi *spi, struct spi_dev *dev) {
    struct gpio_ops *cs_ops = (struct gpio_ops *) dev->cs->obj.ops;

    acquire(&spi->lock);
    cs_ops->set_output_value(dev->cs, 0);
    dev->extended_transaction = 1;
}

static void stm32f4_spi_end_transaction(struct spi *spi, struct spi_dev *dev) {
    struct gpio_ops *cs_ops = (struct gpio_ops *) dev->cs->obj.ops;

    dev->extended_transaction = 0;
    cs_ops->set_output_value(dev->cs, 1);
    release(&spi->lock);
}

struct spi_ops stm32f4_spi_ops = {
    .init = stm32f4_spi_init,
    .deinit = stm32f4_spi_deinit,
    .get_clock = stm32f4_spi_get_clock,
    .set_clock = stm32f4_spi_set_clock,
    .read_write = stm32f4_spi_read_write,
    .read = stm32f4_spi_read,
    .write = stm32f4_spi_write,
    .start_transaction = stm32f4_spi_start_transaction,
    .end_transaction = stm32f4_spi_end_transaction,
};

static int stm32f4_spi_probe(const char *name) {
    const void *blob = fdtparse_get_blob();
    int offset;

    /* Lookup peripheral node */
    offset = fdt_path_offset(blob, name);
    if (offset < 0) {
        return 0;
    }

    /* Check that peripheral is compatible with driver */
    return fdt_node_check_compatible(blob, offset, STM32F4_SPI_COMPAT) == 0;
}

static struct obj *stm32f4_spi_ctor(const char *name) {
    const void *blob = fdtparse_get_blob();
    int offset, err, periph_id, gpio_af;
    struct obj *obj;
    struct spi *spi;
    struct stm32f4_spi_regs *regs;
    struct stm32f4_spi *port;
    enum stm32f4_bus bus;

    offset = fdt_path_offset(blob, name);
    if (offset < 0) {
        return NULL;
    }

    if (fdt_node_check_compatible(blob, offset, STM32F4_SPI_COMPAT)) {
        return NULL;
    }

    regs = fdtparse_get_addr32(blob, offset, "reg");
    if (!regs) {
        return NULL;
    }

    err = fdtparse_get_int(blob, offset, "stmicro,periph-id", &periph_id);
    if (err) {
        return NULL;
    }

    obj = instantiate(name, &spi_class, &stm32f4_spi_ops, struct spi);
    if (!obj) {
        return NULL;
    }

    spi = to_spi(obj);

    init_mutex(&spi->lock);

    spi->priv = kmalloc(sizeof(struct stm32f4_spi));
    if (!spi->priv) {
        goto err_free_obj;
    }

    port = spi->priv;
    memset(port, 0, sizeof(*port));

    port->ready = 0;
    port->periph_id = periph_id;
    port->regs = regs;

    bus = rcc_peripheral_bus(port->periph_id);
    if (bus == STM32F4_UNKNOWN_BUS) {
        goto err_free_port;
    }

    port->periph_clock = rcc_bus_clock(bus);
    if (port->periph_clock <= 0) {
        goto err_free_port;
    }

    gpio_af = gpio_periph_to_alt_func(port->periph_id);
    if (gpio_af == STM32F4_GPIO_AF_UNKNOWN) {
        goto err_free_port;
    }

    /* Setup GPIOs */
    for (int i = 0; i < 3; i++) {
        const char *i2c_gpio_props[3] =
            {"spi,sck-gpio", "spi,miso-gpio", "spi,mosi-gpio"};
        struct fdt_gpio fdt_gpio;
        struct obj *gpio_obj;
        struct gpio *gpio;
        struct gpio_ops *ops;
        int err;

        err = fdtparse_get_gpio(blob, offset, i2c_gpio_props[i], &fdt_gpio);
        if (err) {
            goto err_free_gpio;
        }

        gpio_obj = gpio_get(fdt_gpio.gpio);
        if (!gpio_obj) {
            goto err_free_gpio;
        }

        gpio = to_gpio(gpio_obj);

        ops = gpio_obj->ops;

        /* High speed GPIOs */
        err = ops->set_flags(gpio, STM32F4_GPIO_SPEED,
                             STM32F4_GPIO_SPEED_50MHZ);
        if (err) {
            goto err_free_gpio;
        }

        /* Set to proper SPI mode */
        err = ops->set_flags(gpio, STM32F4_GPIO_ALT_FUNC,
                             gpio_af);
        if (err) {
            goto err_free_gpio;
        }

        port->gpio[i] = gpio;
    }

    /* Export to the OS */
    class_export_member(obj);

    return obj;

err_free_gpio:
    for (int i = 0; i < 3; i++) {
        if (port->gpio[i]) {
            gpio_put(&port->gpio[i]->obj);
        }
    }

err_free_port:
    kfree(port);

err_free_obj:
    class_deinstantiate(obj);

    return NULL;
}

static struct mutex stm32f4_spi_driver_mut = INIT_MUTEX;

static struct device_driver stm32f4_spi_compat_driver = {
    .name = STM32F4_SPI_COMPAT,
    .probe = stm32f4_spi_probe,
    .ctor = stm32f4_spi_ctor,
    .class = &spi_class,
    .mut = &stm32f4_spi_driver_mut,
};

static int stm32f4_spi_register(void) {
    /*
     * Used to automatically generate driver entries for all compatible
     * nodes in the device tree.
     */
    device_compat_driver_register(&stm32f4_spi_compat_driver);
    return 0;
}
CORE_INITIALIZER(stm32f4_spi_register)
