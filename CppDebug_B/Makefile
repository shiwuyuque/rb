CC ?= gcc
CXX ?= g++

CPPFLAGS ?= -I.
CFLAGS ?= -std=c11 -fwrapv -Wall -Wextra -Wpedantic
CXXFLAGS ?= -std=c++17 -fwrapv -Wall -Wextra -Wpedantic

BUILD_DIR := build
CALCULATOR := $(BUILD_DIR)/expression_calculator
GENERATOR := $(BUILD_DIR)/expression_generator
CALCULATOR_OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/expr.o
GENERATOR_OBJECT := $(BUILD_DIR)/gen-expr.o

.PHONY: all calculator generator run generate clean

all: calculator generator

calculator: $(CALCULATOR)

generator: $(GENERATOR)

run: $(CALCULATOR)
	@$(abspath $(CALCULATOR))

generate: $(GENERATOR)
	@$(abspath $(GENERATOR)) $(COUNT)

$(CALCULATOR): $(CALCULATOR_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(GENERATOR): $(GENERATOR_OBJECT)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(CALCULATOR_OBJECTS:.o=.d) $(GENERATOR_OBJECT:.o=.d)

clean:
	rm -rf $(BUILD_DIR)
