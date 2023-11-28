################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../directory/src/pacsat_dir.c \
../directory/src/pacsat_header.c 

C_DEPS += \
./directory/src/pacsat_dir.d \
./directory/src/pacsat_header.d 

OBJS += \
./directory/src/pacsat_dir.o \
./directory/src/pacsat_header.o 


# Each subdirectory must supply rules for building sources it contributes
directory/src/%.o: ../directory/src/%.c directory/src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I../inc -I../broadcast/inc -I../ftl0/inc -I../ax25/inc -I../directory/inc -I/usr/local/include/iors_common -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-directory-2f-src

clean-directory-2f-src:
	-$(RM) ./directory/src/pacsat_dir.d ./directory/src/pacsat_dir.o ./directory/src/pacsat_header.d ./directory/src/pacsat_header.o

.PHONY: clean-directory-2f-src

