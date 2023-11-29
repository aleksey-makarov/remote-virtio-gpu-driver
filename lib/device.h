#ifndef __device_h__
#define __device_h__

void device_reset(void);
unsigned long int device_data_length(void);
unsigned long int device_free_space(void);
int device_put(struct iovec *v, unsigned int n);
int device_get(struct iovec *v, unsigned int n);

#endif