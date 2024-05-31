################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../broadcast/src/pacsat_broadcast.c \
../broadcast/src/pacsat_command.c 

C_DEPS += \
./broadcast/src/pacsat_broadcast.d \
./broadcast/src/pacsat_command.d 

OBJS += \
./broadcast/src/pacsat_broadcast.o \
./broadcast/src/pacsat_command.o 


# Each subdirectory must supply rules for building sources it contributes
broadcast/src/%.o: ../broadcast/src/%.c broadcast/src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I../inc -I../broadcast/inc -I../ftl0/inc -I../directory/inc -I/usr/local/include/iors_common -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-broadcast-2f-src

clean-broadcast-2f-src:
	-$(RM) ./broadcast/src/pacsat_broadcast.d ./broadcast/src/pacsat_broadcast.o ./broadcast/src/pacsat_command.d ./broadcast/src/pacsat_command.o

.PHONY: clean-broadcast-2f-src

