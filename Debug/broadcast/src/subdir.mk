################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../broadcast/src/pacsat_broadcast.c 

C_DEPS += \
./broadcast/src/pacsat_broadcast.d 

OBJS += \
./broadcast/src/pacsat_broadcast.o 


# Each subdirectory must supply rules for building sources it contributes
broadcast/src/%.o: ../broadcast/src/%.c broadcast/src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I"/home/g0kla/Desktop/workspace/Pacsat/inc" -I"/home/g0kla/Desktop/workspace/Pacsat/broadcast/inc" -I"/home/g0kla/Desktop/workspace/Pacsat/ftl0/inc" -I"/home/g0kla/Desktop/workspace/Pacsat/directory/inc" -I"/home/g0kla/Desktop/workspace/Pacsat/ax25/inc" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-broadcast-2f-src

clean-broadcast-2f-src:
	-$(RM) ./broadcast/src/pacsat_broadcast.d ./broadcast/src/pacsat_broadcast.o

.PHONY: clean-broadcast-2f-src

