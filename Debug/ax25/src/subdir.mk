################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ax25/src/ax25_tools.c 

C_DEPS += \
./ax25/src/ax25_tools.d 

OBJS += \
./ax25/src/ax25_tools.o 


# Each subdirectory must supply rules for building sources it contributes
ax25/src/%.o: ../ax25/src/%.c ax25/src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I../inc -I../broadcast/inc -I../ftl0/inc -I../ax25/inc -I../directory/inc -I/usr/local/include/iors_common -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-ax25-2f-src

clean-ax25-2f-src:
	-$(RM) ./ax25/src/ax25_tools.d ./ax25/src/ax25_tools.o

.PHONY: clean-ax25-2f-src

