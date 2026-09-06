## Custom Libraries

Place any custom C++ libraries here. Each library should have:

```
MyLibrary/
├── src/
│   ├── MyLibrary.h
│   ├── MyLibrary.cpp
│   └── ...
└── library.properties
```

### PlatformIO Library Management

To add external libraries, modify `platformio.ini`:

```ini
lib_deps =
    emelianov/modbus-esp8266 @ ^4.1.0
    marcoschwartz/LiquidCrystal_I2C @ ^1.1.4
    MyCustomLib  ; Local library name
```

### Common Libraries Used

- **modbus-esp8266**: Modbus RTU master/slave communication
- **LiquidCrystal_I2C**: I2C 16x2 LCD display driver
