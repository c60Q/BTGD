################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../TC3xx_Boot/source/boot_flash.c \
../TC3xx_Boot/source/boot_flow.c \
../TC3xx_Boot/source/boot_timer.c \
../TC3xx_Boot/source/boot_transport.c 

COMPILED_SRCS += \
./TC3xx_Boot/source/boot_flash.src \
./TC3xx_Boot/source/boot_flow.src \
./TC3xx_Boot/source/boot_timer.src \
./TC3xx_Boot/source/boot_transport.src 

C_DEPS += \
./TC3xx_Boot/source/boot_flash.d \
./TC3xx_Boot/source/boot_flow.d \
./TC3xx_Boot/source/boot_timer.d \
./TC3xx_Boot/source/boot_transport.d 

OBJS += \
./TC3xx_Boot/source/boot_flash.o \
./TC3xx_Boot/source/boot_flow.o \
./TC3xx_Boot/source/boot_timer.o \
./TC3xx_Boot/source/boot_transport.o 


# Each subdirectory must supply rules for building sources it contributes
TC3xx_Boot/source/%.src: ../TC3xx_Boot/source/%.c TC3xx_Boot/source/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING C/C++ Compiler'
	cctc -cs --dep-file="$(basename $@).d" --misrac-version=2004 -D__CPU__=tc39xb "-fD:/fw_code/test/ZCU_Test_Board_Bootloader/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc39xb -Y0 -N0 -Z0 -o "$@" "$<" && \
	if [ -f "$(basename $@).d" ]; then sed.exe -r  -e 's/\b(.+\.o)\b/TC3xx_Boot\/source\/\1/g' -e 's/\\/\//g' -e 's/\/\//\//g' -e 's/"//g' -e 's/([a-zA-Z]:\/)/\L\1/g' -e 's/\d32:/@TARGET_DELIMITER@/g; s/\\\d32/@ESCAPED_SPACE@/g; s/\d32/\\\d32/g; s/@ESCAPED_SPACE@/\\\d32/g; s/@TARGET_DELIMITER@/\d32:/g' "$(basename $@).d" > "$(basename $@).d_sed" && cp "$(basename $@).d_sed" "$(basename $@).d" && rm -f "$(basename $@).d_sed" 2>/dev/null; else echo 'No dependency file to process';fi
	@echo 'Finished building: $<'
	@echo ' '

TC3xx_Boot/source/boot_flash.o: ./TC3xx_Boot/source/boot_flash.src TC3xx_Boot/source/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

TC3xx_Boot/source/boot_flow.o: ./TC3xx_Boot/source/boot_flow.src TC3xx_Boot/source/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

TC3xx_Boot/source/boot_timer.o: ./TC3xx_Boot/source/boot_timer.src TC3xx_Boot/source/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

TC3xx_Boot/source/boot_transport.o: ./TC3xx_Boot/source/boot_transport.src TC3xx_Boot/source/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-TC3xx_Boot-2f-source

clean-TC3xx_Boot-2f-source:
	-$(RM) ./TC3xx_Boot/source/boot_flash.d ./TC3xx_Boot/source/boot_flash.o ./TC3xx_Boot/source/boot_flash.src ./TC3xx_Boot/source/boot_flow.d ./TC3xx_Boot/source/boot_flow.o ./TC3xx_Boot/source/boot_flow.src ./TC3xx_Boot/source/boot_timer.d ./TC3xx_Boot/source/boot_timer.o ./TC3xx_Boot/source/boot_timer.src ./TC3xx_Boot/source/boot_transport.d ./TC3xx_Boot/source/boot_transport.o ./TC3xx_Boot/source/boot_transport.src

.PHONY: clean-TC3xx_Boot-2f-source

