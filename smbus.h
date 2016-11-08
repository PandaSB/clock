#ifndef LIB_I2C_SMBUS_H
#define LIB_I2C_SMBUS_H

#include <linux/types.h>
#include <linux/i2c.h>

extern long i2c_smbus_access(int file, char read_write, unsigned char command,
              int size, union i2c_smbus_data *data);

extern long i2c_smbus_write_quick(int file, unsigned char value);
extern long i2c_smbus_read_byte(int file);
extern long i2c_smbus_write_byte(int file, unsigned char value);
extern long i2c_smbus_read_byte_data(int file, unsigned char command);
extern long i2c_smbus_write_byte_data(int file, unsigned char command, unsigned char value);
extern long i2c_smbus_read_word_data(int file, unsigned char command);
extern long i2c_smbus_write_word_data(int file, unsigned char command, unsigned int value);
extern long i2c_smbus_process_call(int file, unsigned char command, unsigned int value);

/* Returns the number of read bytes */
extern long i2c_smbus_read_block_data(int file, unsigned char command, unsigned char *values);
extern long i2c_smbus_write_block_data(int file, unsigned char command, unsigned char length,
                    const unsigned char *values);

/* Returns the number of read bytes */
/* Until kernel 2.6.22, the length is hardcoded to 32 bytes. If you
   ask for less than 32 bytes, your code will only work with kernels
   2.6.23 and later. */
extern long i2c_smbus_read_i2c_block_data(int file, unsigned char command, unsigned char length,
                       unsigned char *values);
extern long i2c_smbus_write_i2c_block_data(int file, unsigned char command, unsigned char length,
                        const unsigned char *values);

/* Returns the number of read bytes */
extern long i2c_smbus_block_process_call(int file, unsigned char command, unsigned char length,
                      unsigned char *values);

#endif /* LIB_I2C_SMBUS_H */
