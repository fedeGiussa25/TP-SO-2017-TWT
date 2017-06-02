################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: kernelproto

# Tool invocations
kernelproto: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	gcc  -o "kernelproto" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
<<<<<<< Updated upstream
	-$(RM) $(OBJS)$(C_DEPS)$(EXECUTABLES) Kernel
=======
<<<<<<< Updated upstream
	-$(RM) $(EXECUTABLES)$(OBJS)$(C_DEPS) Kernel
=======
	-$(RM) $(EXECUTABLES)$(OBJS)$(C_DEPS) kernelproto
>>>>>>> Stashed changes
>>>>>>> Stashed changes
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
