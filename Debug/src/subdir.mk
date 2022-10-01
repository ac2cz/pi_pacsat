################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/agw_tnc.c \
../src/pacsat_main.c \
../src/util.c 

C_DEPS += \
./src/agw_tnc.d \
./src/pacsat_main.d \
./src/util.d 

OBJS += \
./src/agw_tnc.o \
./src/pacsat_main.o \
./src/util.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I"/home/g0kla/Desktop/workspace/Pacsat/inc" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/agw_tnc.d ./src/agw_tnc.o ./src/pacsat_main.d ./src/pacsat_main.o ./src/util.d ./src/util.o

.PHONY: clean-src

