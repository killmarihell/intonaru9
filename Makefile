# INTONARUMORI 0.9 - Makefile (devkitARM + libctru)
# make        -> gera intonarumori.3dsx (precisa de DEVKITPRO)
# make host   -> compila e roda os testes no PC (sem devkitPro)

ifeq ($(filter host intro,$(MAKECMDGOALS)),)
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO nao definido. Use o terminal do devkitPro ou a imagem docker devkitpro/devkitarm)
endif
endif

export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITPRO)/devkitARM/bin:$(PATH)

TARGET  := intonarumori
CC      := arm-none-eabi-gcc
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -std=gnu11 -mword-relocations -ffunction-sections $(ARCH) \
           -D__3DS__ -I$(DEVKITPRO)/libctru/include -Isource
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(TARGET).map -L$(DEVKITPRO)/libctru/lib
LIBS    := -lctru -lm

SRC := $(wildcard source/*.c)
OBJ := $(SRC:source/%.c=build/%.o)

.PHONY: all clean host intro
all: $(TARGET).3dsx

# icone/titulo do Homebrew Launcher: opcional (se o smdhtool falhar, o .3dsx sai igual)
$(TARGET).smdh: icon.png
	-smdhtool --create "INTONARUMORI" "Noise + ambient instrument" "VFD" icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ $(if $(wildcard $(TARGET).smdh),--smdh=$(TARGET).smdh)

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

build/%.o: source/%.c | build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build:
	mkdir -p build

# testes do motor, da UI e do gravador no PC (nao usa libctru)
host: | build
	cc -O2 -std=gnu11 -Wall host/test_host.c source/synth.c source/amb.c source/rec.c source/ui.c source/art.c source/gfx.c -lm -o build/test_host
	mkdir -p previews && ./build/test_host previews

# abertura VFD no PC: renderiza os 90 quadros das duas telas em previews/intro_*.ppm (com ASAN/UBSAN)
intro: | build
	cc -O1 -g -fsanitize=address,undefined -std=gnu11 -Wall -Isource host/test_intro.c source/art.c source/gfx.c source/ui.c source/synth.c source/amb.c -lm -o build/test_intro
	mkdir -p previews && ./build/test_intro previews

clean:
	rm -rf build $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).map

-include $(OBJ:.o=.d)
