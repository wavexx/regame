## Makefile for regame
## Copyright(c) 2003 by wave++ "Yuri D'Elia" <wavexx@users.sf.net>
## Distributed under GNU LGPL WITHOUT ANY WARRANTY.

FLTK_CONFIG = fltk-config
FLTK_FLUID = fluid
FLTK_FLAGS = --use-gl
CXXFLAGS = -O3
CPPFLAGS = -DGAMEDIR='"/usr/local/share/regame"'
LDFLAGS += -lpng -lGL $(shell $(FLTK_CONFIG) $(FLTK_FLAGS) --ldflags)
LDADD += $(shell $(FLTK_CONFIG) $(FLTK_FLAGS) --libs)


# Config
REGAME_OBJECTS = regame.o score.o
TARGETS = regame


# Rules
.SUFFIXES: .cc .o .fl
.PHONY: all clean

.cc.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.fl.cc:
	$(FLTK_FLUID) -c $<


# Targets
all: $(TARGETS)

regame.cc: score.cc

regame: $(REGAME_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(REGAME_OBJECTS) $(LDADD)

clean:
	rm -rf *.o *.d core ii_files $(TARGETS)


# Dependencies
regame.cc: score.cc
