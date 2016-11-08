#include <errno.h>
#include "smbus.h"
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define NULL 0


/* Compatibility defines */
#ifndef I2C_SMBUS_I2C_BLOCK_BROKEN
#define I2C_SMBUS_I2C_BLOCK_BROKEN I2C_SMBUS_I2C_BLOCK_DATA
#endif
#ifndef I2C_FUNC_SMBUS_PEC
#define I2C_FUNC_SMBUS_PEC I2C_FUNC_SMBUS_HWPEC_CALC
#endif

long i2c_smbus_access(int file, char read_write, unsigned char command,
           int size, union i2c_smbus_data *data)
{
    struct i2c_smbus_ioctl_data args;
    long err;

    args.read_write = read_write;
    args.command = command;
    args.size = size;
    args.data = data;

    err = ioctl(file, I2C_SMBUS, &args);
    if (err == -1)
        err = -errno;
    return err;
}


long i2c_smbus_write_quick(int file, unsigned char value)
{
    return i2c_smbus_access(file, value, 0, I2C_SMBUS_QUICK, NULL);
}

long i2c_smbus_read_byte(int file)
{
    union i2c_smbus_data data;
    int err;

    err = i2c_smbus_access(file, I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &data);
    if (err < 0)
        return err;

    return 0x0FF & data.byte;
}

long i2c_smbus_write_byte(int file, unsigned char value)
{
    return i2c_smbus_access(file, I2C_SMBUS_WRITE, value,
                I2C_SMBUS_BYTE, NULL);
}

long i2c_smbus_read_byte_data(int file, unsigned char command)
{
    union i2c_smbus_data data;
    int err;

    err = i2c_smbus_access(file, I2C_SMBUS_READ, command,
                   I2C_SMBUS_BYTE_DATA, &data);
    if (err < 0)
        return err;

    return 0x0FF & data.byte;
}

long i2c_smbus_write_byte_data(int file, unsigned char command, unsigned char value)
{
    union i2c_smbus_data data;
    data.byte = value;
    return i2c_smbus_access(file, I2C_SMBUS_WRITE, command,
                I2C_SMBUS_BYTE_DATA, &data);
}

long i2c_smbus_read_word_data(int file, unsigned char command)
{
    union i2c_smbus_data data;
    int err;

    err = i2c_smbus_access(file, I2C_SMBUS_READ, command,
                   I2C_SMBUS_WORD_DATA, &data);
    if (err < 0)
        return err;

    return 0x0FFFF & data.word;
}

long i2c_smbus_write_word_data(int file, unsigned char command, unsigned int value)
{
    union i2c_smbus_data data;
    data.word = value;
    return i2c_smbus_access(file, I2C_SMBUS_WRITE, command,
                I2C_SMBUS_WORD_DATA, &data);
}

long i2c_smbus_process_call(int file, unsigned char command, unsigned int value)
{
    union i2c_smbus_data data;
    data.word = value;
    if (i2c_smbus_access(file, I2C_SMBUS_WRITE, command,
                 I2C_SMBUS_PROC_CALL, &data))
        return -1;
    else
        return 0x0FFFF & data.word;
}

/* Returns the number of read bytes */
long i2c_smbus_read_block_data(int file, unsigned char command, unsigned char *values)
{
    union i2c_smbus_data data;
    int i, err;

    err = i2c_smbus_access(file, I2C_SMBUS_READ, command,
                   I2C_SMBUS_BLOCK_DATA, &data);
    if (err < 0)
        return err;

    for (i = 1; i <= data.block[0]; i++)
        values[i-1] = data.block[i];
    return data.block[0];
}

long i2c_smbus_write_block_data(int file, unsigned char command, unsigned char length,
                 const unsigned char *values)
{
    union i2c_smbus_data data;
    int i;
    if (length > I2C_SMBUS_BLOCK_MAX)
        length = I2C_SMBUS_BLOCK_MAX;
    for (i = 1; i <= length; i++)
        data.block[i] = values[i-1];
    data.block[0] = length;
    return i2c_smbus_access(file, I2C_SMBUS_WRITE, command,
                I2C_SMBUS_BLOCK_DATA, &data);
}

/* Returns the number of read bytes */
/* Until kernel 2.6.22, the length is hardcoded to 32 bytes. If you
   ask for less than 32 bytes, your code will only work with kernels
   2.6.23 and later. */
long i2c_smbus_read_i2c_block_data(int file, unsigned char command, unsigned char length,
                    unsigned char *values)
{
    union i2c_smbus_data data;
    int i, err;

    if (length > I2C_SMBUS_BLOCK_MAX)
        length = I2C_SMBUS_BLOCK_MAX;
    data.block[0] = length;

    err = i2c_smbus_access(file, I2C_SMBUS_READ, command,
                   length == 32 ? I2C_SMBUS_I2C_BLOCK_BROKEN :
                I2C_SMBUS_I2C_BLOCK_DATA, &data);
    if (err < 0)
        return err;

    for (i = 1; i <= data.block[0]; i++)
        values[i-1] = data.block[i];
    return data.block[0];
}

long i2c_smbus_write_i2c_block_data(int file, unsigned char command, unsigned char length,
                     const unsigned char *values)
{
    union i2c_smbus_data data;
    int i;
    if (length > I2C_SMBUS_BLOCK_MAX)
        length = I2C_SMBUS_BLOCK_MAX;
    for (i = 1; i <= length; i++)
        data.block[i] = values[i-1];
    data.block[0] = length;
    return i2c_smbus_access(file, I2C_SMBUS_WRITE, command,
                I2C_SMBUS_I2C_BLOCK_BROKEN, &data);
}

/* Returns the number of read bytes */
long i2c_smbus_block_process_call(int file, unsigned char command, unsigned char length,
                   unsigned char *values)
{
    union i2c_smbus_data data;
    int i, err;

    if (length > I2C_SMBUS_BLOCK_MAX)
        length = I2C_SMBUS_BLOCK_MAX;
    for (i = 1; i <= length; i++)
        data.block[i] = values[i-1];
    data.block[0] = length;

    err = i2c_smbus_access(file, I2C_SMBUS_WRITE, command,
                   I2C_SMBUS_BLOCK_PROC_CALL, &data);
    if (err < 0)
        return err;

    for (i = 1; i <= data.block[0]; i++)
        values[i-1] = data.block[i];
    return data.block[0];
}
