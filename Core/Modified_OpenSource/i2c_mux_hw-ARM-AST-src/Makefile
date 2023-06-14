DEBUG := n
TARGET := i2c_mux_hw
OBJS := ast_i2c_mux.o

# Kernel build switch
EXTRA_CFLAGS += -DLINUX_KERNEL_BUILD

EXTRA_CFLAGS += -I${SPXINC}/global
EXTRA_CFLAGS += -I${SPXINC}/i2cmux
EXTRA_CFLAGS += -I${SPXINC}/helper

KBUILD_EXTRA_SYMBOLS += ${SPXINC}/i2cmux/Module.symvers
KBUILD_EXTRA_SYMBOLS += ${SPXINC}/helper/Module.symvers

-include ${TOOLDIR}/rules/Rules.make.drivers
