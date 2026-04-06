#ifndef DS18B20_H
#define DS18B20_H

char ds_init(void);
void ds_convert(void);
char ds_temp(unsigned char *);

#endif
