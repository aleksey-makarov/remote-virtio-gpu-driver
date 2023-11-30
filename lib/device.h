#ifndef __device_h__
#define __device_h__

void device_reset(void);

unsigned long int device_get_data_length(void);
unsigned long int device_get_buffer_size(void);
static inline unsigned long int device_get_free_space(void)
{
	return device_get_buffer_size() - device_get_data_length();
}

int device_put(struct iovec *v, unsigned int n);
int device_get(struct iovec *v, unsigned int n);

#endif