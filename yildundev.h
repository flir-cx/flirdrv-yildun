#ifndef YILDUNDEV_H
#define YILDUNDEV_H

#define YILDUN_IOCTL_W(code,type)   _IOW('y', code, type)
#define YILDUN_IOCTL_R(code,type)   _IOR('y', code, type)
#define YILDUN_IOCTL_WR(code,type)  _IOWR('y', code, type)
#define YILDUN_IOCTL_NWR(code)      _IO('y', code)

#define IOCTL_YILDUN_ENABLE	YILDUN_IOCTL_NWR(1)
#define IOCTL_YILDUN_DISABLE	YILDUN_IOCTL_NWR(2)

#endif
