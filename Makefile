PLUGIN = playback-control.so
SRCS = playback-control.cc

# We use pkg-config to get all the C++ flags and Includes automatically
CXXFLAGS += -std=c++17 -fPIC -Wall -Wextra \
            $(shell pkg-config --cflags audacious Qt6Core Qt6Widgets Qt6Gui)

# We use pkg-config to get the library links automatically
LIBS += $(shell pkg-config --libs audacious Qt6Core Qt6Widgets Qt6Gui)

all: $(PLUGIN)

$(PLUGIN): $(SRCS)
	g++ $(CXXFLAGS) -shared -o $@ $(SRCS) $(LIBS)

clean:
	rm -f $(PLUGIN)

install: $(PLUGIN)
	sudo cp $(PLUGIN) /usr/lib/audacious/General/