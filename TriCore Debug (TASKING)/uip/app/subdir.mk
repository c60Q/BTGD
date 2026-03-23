################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../uip/app/IfxGeth_Phy_88E1512.c \
../uip/app/IfxGeth_Phy_Dp83825i.c \
../uip/app/clock-arch.c \
../uip/app/net.c \
../uip/app/netdev.c 

COMPILED_SRCS += \
./uip/app/IfxGeth_Phy_88E1512.src \
./uip/app/IfxGeth_Phy_Dp83825i.src \
./uip/app/clock-arch.src \
./uip/app/net.src \
./uip/app/netdev.src 

C_DEPS += \
./uip/app/IfxGeth_Phy_88E1512.d \
./uip/app/IfxGeth_Phy_Dp83825i.d \
./uip/app/clock-arch.d \
./uip/app/net.d \
./uip/app/netdev.d 

OBJS += \
./uip/app/IfxGeth_Phy_88E1512.o \
./uip/app/IfxGeth_Phy_Dp83825i.o \
./uip/app/clock-arch.o \
./uip/app/net.o \
./uip/app/netdev.o 


# Each subdirectory must supply rules for building sources it contributes
uip/app/%.src: ../uip/app/%.c uip/app/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING C/C++ Compiler'
	cctc -cs --dep-file="$(basename $@).d" --misrac-version=2004 -D__CPU__=tc39xb "-fD:/fw_code/test/ZCU_Test_Board_Bootloader/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc39xb -Y0 -N0 -Z0 -o "$@" "$<" && \
	if [ -f "$(basename $@).d" ]; then sed.exe -r  -e 's/\b(.+\.o)\b/uip\/app\/\1/g' -e 's/\\/\//g' -e 's/\/\//\//g' -e 's/"//g' -e 's/([a-zA-Z]:\/)/\L\1/g' -e 's/\d32:/@TARGET_DELIMITER@/g; s/\\\d32/@ESCAPED_SPACE@/g; s/\d32/\\\d32/g; s/@ESCAPED_SPACE@/\\\d32/g; s/@TARGET_DELIMITER@/\d32:/g' "$(basename $@).d" > "$(basename $@).d_sed" && cp "$(basename $@).d_sed" "$(basename $@).d" && rm -f "$(basename $@).d_sed" 2>/dev/null; else echo 'No dependency file to process';fi
	@echo 'Finished building: $<'
	@echo ' '

uip/app/IfxGeth_Phy_88E1512.o: ./uip/app/IfxGeth_Phy_88E1512.src uip/app/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/app/IfxGeth_Phy_Dp83825i.o: ./uip/app/IfxGeth_Phy_Dp83825i.src uip/app/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/app/clock-arch.o: ./uip/app/clock-arch.src uip/app/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/app/net.o: ./uip/app/net.src uip/app/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

uip/app/netdev.o: ./uip/app/netdev.src uip/app/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: TASKING Assembler'
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-uip-2f-app

clean-uip-2f-app:
	-$(RM) ./uip/app/IfxGeth_Phy_88E1512.d ./uip/app/IfxGeth_Phy_88E1512.o ./uip/app/IfxGeth_Phy_88E1512.src ./uip/app/IfxGeth_Phy_Dp83825i.d ./uip/app/IfxGeth_Phy_Dp83825i.o ./uip/app/IfxGeth_Phy_Dp83825i.src ./uip/app/clock-arch.d ./uip/app/clock-arch.o ./uip/app/clock-arch.src ./uip/app/net.d ./uip/app/net.o ./uip/app/net.src ./uip/app/netdev.d ./uip/app/netdev.o ./uip/app/netdev.src

.PHONY: clean-uip-2f-app

