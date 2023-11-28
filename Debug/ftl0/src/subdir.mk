################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ftl0/src/ftl0.c 

C_DEPS += \
./ftl0/src/ftl0.d 

OBJS += \
./ftl0/src/ftl0.o 


# Each subdirectory must supply rules for building sources it contributes
ftl0/src/%.o: ../ftl0/src/%.c ftl0/src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I../inc -I../broadcast/inc -I../ftl0/inc -I../ax25/inc -I../directory/inc -I/usr/local/include/iors_common -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-ftl0-2f-src

clean-ftl0-2f-src:
	-$(RM) ./ftl0/src/ftl0.d ./ftl0/src/ftl0.o

.PHONY: clean-ftl0-2f-src

