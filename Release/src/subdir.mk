################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/config.c \
../src/pacsat_main.c \
../src/state_file.c \
../src/telemetry.c 

C_DEPS += \
./src/config.d \
./src/pacsat_main.d \
./src/state_file.d \
./src/telemetry.d 

OBJS += \
./src/config.o \
./src/pacsat_main.o \
./src/state_file.o \
./src/telemetry.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -DIORS_CONTROL_BUILD=1 -I../inc -I../broadcast/inc -I../ftl0/inc -I../directory/inc -I/usr/local/include/iors_common -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/config.d ./src/config.o ./src/pacsat_main.d ./src/pacsat_main.o ./src/state_file.d ./src/state_file.o ./src/telemetry.d ./src/telemetry.o

.PHONY: clean-src

