################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ax25_tools.c 

C_DEPS += \
./ax25_tools.d 

OBJS += \
./ax25_tools.o 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I"/home/g0kla/Desktop/workspace/Pacsat/inc" -I"/home/g0kla/Desktop/workspace/Pacsat/broadcast/inc" -I"/home/g0kla/Desktop/workspace/Pacsat/ftl0/inc" -I"/home/g0kla/Desktop/workspace/Pacsat/directory/inc" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean--2e-

clean--2e-:
	-$(RM) ./ax25_tools.d ./ax25_tools.o

.PHONY: clean--2e-

