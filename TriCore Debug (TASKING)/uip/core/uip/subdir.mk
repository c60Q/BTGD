################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../uip/core/uip/psock.c \
../uip/core/uip/uip-fw.c \
../uip/core/uip/uip-neighbor.c \
../uip/core/uip/uip.c \
../uip/core/uip/uip_arp.c \
../uip/core/uip/uip_timer.c \
../uip/core/uip/uiplib.c 

COMPILED_SRCS += \
./uip/core/uip/psock.src \
./uip/core/uip/uip-fw.src \
./uip/core/uip/uip-neighbor.src \
./uip/core/uip/uip.src \
./uip/core/uip/uip_arp.src \
./uip/core/uip/uip_timer.src \
./uip/core/uip/uiplib.src 

C_DEPS += \
./uip/core/uip/psock.d \
./uip/core/uip/uip-fw.d \
./uip/core/uip/uip-neighbor.d \
./uip/core/uip/uip.d \
./uip/core/uip/uip_arp.d \
./uip/core/uip/uip_timer.d \
./uip/core/uip/uiplib.d 

OBJS += \
./uip/core/uip/psock.o \
./uip/core/uip/uip-fw.o \
./uip/core/uip/uip-neighbor.o \
./uip/core/uip/uip.o \
./uip/core/uip/uip_arp.o \
./uip/core/uip/uip_timer.o \
./uip/core/uip/uiplib.o 


# Each subdirectory must supply rules for building sources it contributes
uip/core/uip/%.src: ../uip/core/uip/%.c uip/core/uip/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING C/C++ Compiler'
	cctc -cs --dep-file="$(basename $@).d" --misrac-version=2004 -D__CPU__=tc39xb "-fD:/fw_code/test/ZCU_Test_Board_Bootloader/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc39xb -Y0 -N0 -Z0 -o "$@" "$<" && \
	if [ -f "$(basename $@).d" ]; then sed.exe -r  -e 's/\b(.+\.o)\b/uip\/core\/uip\/\1/g' -e 's/\\/\//g' -e 's/\/\//\//g' -e 's/"//g' -e 's/([a-zA-Z]:\/)/\L\1/g' -e 's/\d32:/@TARGET_DELIMITER@/g; s/\\\d32/@ESCAPED_SPACE@/g; s/\d32/\\\d32/g; s/@ESCAPED_SPACE@/\\\d32/g; s/@TARGET_DELIMITER@/\d32:/g' "$(basename $@).d" > "$(basename $@).d_sed" && cp "$(basename $@).d_sed" "$(basename $@).d" && rm -f "$(basename $@).d_sed" 2>/dev/null; else echo 'No dependency file to process';fi
	@echo 'Finished building: $<'
	@echo ' '

uip/core/uip/psock.o: ./uip/core/uip/psock.src uip/core/uip/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/core/uip/uip-fw.o: ./uip/core/uip/uip-fw.src uip/core/uip/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/core/uip/uip-neighbor.o: ./uip/core/uip/uip-neighbor.src uip/core/uip/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/core/uip/uip.o: ./uip/core/uip/uip.src uip/core/uip/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/core/uip/uip_arp.o: ./uip/core/uip/uip_arp.src uip/core/uip/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/core/uip/uip_timer.o: ./uip/core/uip/uip_timer.src uip/core/uip/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/core/uip/uiplib.o: ./uip/core/uip/uiplib.src uip/core/uip/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-uip-2f-core-2f-uip

clean-uip-2f-core-2f-uip:
	-$(RM) ./uip/core/uip/psock.d ./uip/core/uip/psock.o ./uip/core/uip/psock.src ./uip/core/uip/uip-fw.d ./uip/core/uip/uip-fw.o ./uip/core/uip/uip-fw.src ./uip/core/uip/uip-neighbor.d ./uip/core/uip/uip-neighbor.o ./uip/core/uip/uip-neighbor.src ./uip/core/uip/uip.d ./uip/core/uip/uip.o ./uip/core/uip/uip.src ./uip/core/uip/uip_arp.d ./uip/core/uip/uip_arp.o ./uip/core/uip/uip_arp.src ./uip/core/uip/uip_timer.d ./uip/core/uip/uip_timer.o ./uip/core/uip/uip_timer.src ./uip/core/uip/uiplib.d ./uip/core/uip/uiplib.o ./uip/core/uip/uiplib.src

.PHONY: clean-uip-2f-core-2f-uip

