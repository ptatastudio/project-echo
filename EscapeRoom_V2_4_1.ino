/*
====================================================
           ESCAPE ROOM - VERSION 2.4.1
====================================================

PLACA: ARDUINO MEGA 2560

COMPONENTES:
- LCD 1602 de 16 pines
- Teclado matricial 4x4
- Buzzer pasivo
- Display 5461AS de catodo comun
- 4 transistores 2N2222A
- LED RGB de catodo comun (rojo y verde)

FUNCIONES:
- Contrasena aleatoria de 4 digitos, con ceros iniciales
- Evita repetir inmediatamente la contrasena anterior
- GETCODE devuelve CODE:dddd antes de iniciar
- START inicia la investigacion de 60 segundos
- Al terminar la investigacion, habilita el teclado y comienza la cuenta atras fisica de 60 segundos
- Envia TECLADO HABILITADO al iniciar el minuto final de la bomba
- Conserva pitidos progresivos y efectos de LEDs

PROTOCOLO USB SERIE: 9600 baudios, texto terminado en LF
COMANDOS: GETCODE y START
====================================================
*/

#include <Keypad.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <string.h>
#include <stdio.h>

// --------------------------------------------------
// 1. CONFIGURACION GENERAL
// --------------------------------------------------
const unsigned long DURACION_INICIAL = 60UL;
const unsigned long DURACION_INVESTIGACION = 60UL;

const int BUZZER = 36;
const int LED_ROJO = 49;
const int LED_VERDE = 50;

const unsigned long PITIDO_INICIAL = 3000UL;
const unsigned long PITIDO_FINAL = 250UL;

// EEPROM: se guarda el ultimo codigo para evitar
// que se repita inmediatamente en el siguiente reset.
const int EEPROM_DIRECCION_ULTIMA_CLAVE = 0;

// --------------------------------------------------
// 2. CONTRASENA DE LA PARTIDA
// --------------------------------------------------
char PASSWORD[5] = "0000";

// --------------------------------------------------
// 3. LCD 1602
// Pines: RS, E, D4, D5, D6, D7
// --------------------------------------------------
LiquidCrystal lcd(30, 31, 32, 33, 34, 35);

// --------------------------------------------------
// 4. TECLADO MATRICIAL 4x4
// --------------------------------------------------
const byte FILAS = 4;
const byte COLUMNAS = 4;

char teclas[FILAS][COLUMNAS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte pinesFilas[FILAS] = {22, 23, 24, 25};
byte pinesColumnas[COLUMNAS] = {26, 27, 28, 29};

Keypad teclado = Keypad(
  makeKeymap(teclas),
  pinesFilas,
  pinesColumnas,
  FILAS,
  COLUMNAS
);

// --------------------------------------------------
// 5. DISPLAY 5461AS, CATODO COMUN
// Segmentos: A, B, C, D, E, F, G, DP
// Digitos: DIG1, DIG2, DIG3, DIG4
// --------------------------------------------------
const byte segmentos[8] = {37, 38, 39, 40, 41, 42, 43, 44};
const byte digitos[4] = {45, 46, 47, 48};

const byte numeros[10][7] = {
  {1,1,1,1,1,1,0}, // 0
  {0,1,1,0,0,0,0}, // 1
  {1,1,0,1,1,0,1}, // 2
  {1,1,1,1,0,0,1}, // 3
  {0,1,1,0,0,1,1}, // 4
  {1,0,1,1,0,1,1}, // 5
  {1,0,1,1,1,1,1}, // 6
  {1,1,1,0,0,0,0}, // 7
  {1,1,1,1,1,1,1}, // 8
  {1,1,1,1,0,1,1}  // 9
};

byte digitosPantalla[4] = {0, 3, 0, 0};
byte digitoActual = 0;

// --------------------------------------------------
// 6. ESTADOS DEL JUEGO
// --------------------------------------------------
enum EstadoJuego {
  ESPERANDO_INICIO,
  JUGANDO,
  DESACTIVADO,
  TIEMPO_AGOTADO
};

EstadoJuego estado = ESPERANDO_INICIO;

// --------------------------------------------------
// 7. VARIABLES DE CONTRASENA Y TECLADO
// --------------------------------------------------
char claveIntroducida[5] = "";
byte longitudClave = 0;
bool tecladoHabilitado = false;
unsigned long inicioPartida = 0;

// --------------------------------------------------
// 8. CRONOMETRO
// --------------------------------------------------
unsigned long tiempoRestante = DURACION_INICIAL;
unsigned long marcaSegundo = 0;

// --------------------------------------------------
// 9. BUZZER
// --------------------------------------------------
unsigned long ultimoPitido = 0;

// --------------------------------------------------
// 10. FEEDBACK LED ROJO
// --------------------------------------------------
unsigned long proximoParpadeo = 0;
bool ledRojoEstado = false;
bool feedbackActivo = false;
bool feedbackRojo = false;
unsigned long finFeedback = 0;

// --------------------------------------------------
// 11. ALARMA FINAL
// --------------------------------------------------
unsigned long proximaAlarma = 0;
bool alternarAlarma = false;

// --------------------------------------------------
// 12. COMANDOS SERIE USB
// --------------------------------------------------
char comandoSerie[16];
byte indiceComando = 0;

// --------------------------------------------------
// 13. INICIALIZAR ALEATORIEDAD
// A0 debe permanecer sin conectar.
// Es suficiente para un minijuego, no para seguridad.
// --------------------------------------------------
void inicializarAleatoriedad() {
  pinMode(A0, INPUT);

  unsigned long semilla = micros();

  for (byte i = 0; i < 32; i++) {
    unsigned int lectura = analogRead(A0);
    semilla ^= ((unsigned long)lectura << ((i % 4) * 8));
    semilla = (semilla << 5) | (semilla >> 27);
    delayMicroseconds(300);
  }

  semilla ^= micros();
  semilla ^= millis();

  if (semilla == 0) {
    semilla = 1;
  }

  randomSeed(semilla);
}

// --------------------------------------------------
// 14. GENERAR CLAVE ALEATORIA DE 4 DIGITOS
// Conserva ceros iniciales. EEPROM evita repeticion
// consecutiva, pero no garantiza unicidad historica.
// --------------------------------------------------
void generarContrasena() {
  uint16_t codigo = (uint16_t)random(0L, 10000L);
  uint16_t codigoAnterior = 0xFFFF;

  EEPROM.get(EEPROM_DIRECCION_ULTIMA_CLAVE, codigoAnterior);

  if (codigoAnterior <= 9999 && codigo == codigoAnterior) {
    codigo = (uint16_t)((codigo + 1U) % 10000U);
  }

  EEPROM.put(EEPROM_DIRECCION_ULTIMA_CLAVE, codigo);

  snprintf(PASSWORD, sizeof(PASSWORD), "%04u", (unsigned int)codigo);
}

// --------------------------------------------------
// 15. INTERVALO PROGRESIVO DE PITIDOS
// De 3000 ms al inicio a 250 ms al final.
// --------------------------------------------------
unsigned long obtenerIntervaloPitido() {
  unsigned long transcurrido = DURACION_INICIAL - tiempoRestante;
  unsigned long reduccion =
    (transcurrido * (PITIDO_INICIAL - PITIDO_FINAL)) / DURACION_INICIAL;

  return PITIDO_INICIAL - reduccion;
}

// --------------------------------------------------
// 16. ACTUALIZAR DIGITOS DEL CRONOMETRO
// --------------------------------------------------
void actualizarDigitosTiempo() {
  unsigned int minutos = tiempoRestante / 60UL;
  unsigned int segundos = tiempoRestante % 60UL;

  digitosPantalla[0] = (minutos / 10U) % 10U;
  digitosPantalla[1] = minutos % 10U;
  digitosPantalla[2] = segundos / 10U;
  digitosPantalla[3] = segundos % 10U;
}

// --------------------------------------------------
// 17. MULTIPLEXACION DEL DISPLAY
// --------------------------------------------------
void refrescarDisplay() {
  for (byte i = 0; i < 4; i++) {
    digitalWrite(digitos[i], LOW);
  }

  byte numero = digitosPantalla[digitoActual];

  for (byte s = 0; s < 7; s++) {
    digitalWrite(segmentos[s], numeros[numero][s] ? HIGH : LOW);
  }

  // Punto decimal entre minutos y segundos.
  digitalWrite(segmentos[7], digitoActual == 1 ? HIGH : LOW);

  digitalWrite(digitos[digitoActual], HIGH);
  delayMicroseconds(1500);
  digitalWrite(digitos[digitoActual], LOW);

  digitoActual++;
  if (digitoActual >= 4) {
    digitoActual = 0;
  }
}

// --------------------------------------------------
// 18. PANTALLAS LCD
// --------------------------------------------------
void mostrarPantallaEspera() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ESPERANDO INICIO");
  lcd.setCursor(0, 1);
  lcd.print("Enviar START USB");
}

void mostrarPantallaInvestigacion() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RECUPERANDO...");
  lcd.setCursor(0, 1);
  lcd.print("ESPERA EN LA WEB");
}

void mostrarPantallaClave() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("INTRODUZCA CLAVE");
  lcd.setCursor(0, 1);
  lcd.print("Clave: ");

  // Mostrar los digitos introducidos, no asteriscos.
  for (byte i = 0; i < 4; i++) {
    if (i < longitudClave) {
      lcd.print(claveIntroducida[i]);
    } else {
      lcd.print("_");
    }
  }
}

// --------------------------------------------------
// 19. HABILITAR TECLADO AL TERMINAR LOS 60 SEGUNDOS
// --------------------------------------------------
void actualizarHabilitacionTeclado(unsigned long ahora) {
  if (estado != JUGANDO || tecladoHabilitado) {
    return;
  }

  if (ahora - inicioPartida >= DURACION_INVESTIGACION * 1000UL) {
    tecladoHabilitado = true;

    // El minuto de la bomba empieza AHORA, cuando el jugador ya puede
    // introducir la clave. Durante la investigacion se mantiene en 01:00.
    marcaSegundo = ahora;
    ultimoPitido = ahora;
    actualizarDigitosTiempo();

    mostrarPantallaClave();
    Serial.println("TECLADO HABILITADO");
  }
}

// --------------------------------------------------
// 20. INICIAR PARTIDA
// --------------------------------------------------
void iniciarJuego() {
  if (estado != ESPERANDO_INICIO) {
    Serial.println("EL JUEGO YA HA COMENZADO");
    return;
  }

  tiempoRestante = DURACION_INICIAL;
  actualizarDigitosTiempo();

  unsigned long ahora = millis();
  inicioPartida = ahora;
  marcaSegundo = ahora;
  ultimoPitido = ahora;

  longitudClave = 0;
  claveIntroducida[0] = '\0';
  tecladoHabilitado = false;

  feedbackActivo = false;
  feedbackRojo = false;
  ledRojoEstado = false;

  digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_VERDE, LOW);
  noTone(BUZZER);

  estado = JUGANDO;
  mostrarPantallaInvestigacion();

  Serial.println("JUEGO INICIADO");
}

// --------------------------------------------------
// 21. COMANDOS USB
// GETCODE devuelve CODE:dddd antes de empezar.
// START inicia la partida.
// --------------------------------------------------
void enviarContrasena() {
  if (estado == ESPERANDO_INICIO) {
    Serial.print("CODE:");
    Serial.println(PASSWORD);
  } else {
    Serial.println("CODE NO DISPONIBLE EN ESTA FASE");
  }
}

void procesarComandoSerial() {
  while (Serial.available() > 0) {
    char caracter = (char)Serial.read();

    if (caracter == '\r') {
      continue;
    }

    if (caracter == '\n') {
      if (indiceComando == 0) {
        continue;
      }

      comandoSerie[indiceComando] = '\0';

      if (strcmp(comandoSerie, "GETCODE") == 0) {
        enviarContrasena();
      } else if (strcmp(comandoSerie, "START") == 0) {
        iniciarJuego();
      } else {
        Serial.println("COMANDO NO RECONOCIDO");
      }

      indiceComando = 0;
    } else {
      if (caracter >= 'a' && caracter <= 'z') {
        caracter = (char)(caracter - 'a' + 'A');
      }

      if (indiceComando < sizeof(comandoSerie) - 1) {
        comandoSerie[indiceComando++] = caracter;
      } else {
        indiceComando = 0;
        Serial.println("COMANDO DEMASIADO LARGO");
      }
    }
  }
}

// --------------------------------------------------
// 22. FEEDBACK VISUAL DE ERROR (SIN PITIDO EXTRA)
// El pitido progresivo del cronometro continua.
// --------------------------------------------------
void iniciarFeedback(const char* linea1,
                     const char* linea2,
                     unsigned long duracion,
                     bool parpadearRojo) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(linea1);
  lcd.setCursor(0, 1);
  lcd.print(linea2);

  feedbackActivo = true;
  feedbackRojo = parpadearRojo;
  finFeedback = millis() + duracion;
  ledRojoEstado = parpadearRojo;

  digitalWrite(LED_ROJO, parpadearRojo ? HIGH : LOW);
  digitalWrite(LED_VERDE, LOW);
  proximoParpadeo = millis() + 200UL;
}

void actualizarFeedback(unsigned long ahora) {
  if (!feedbackActivo) {
    return;
  }

  if (feedbackRojo && ahora >= proximoParpadeo) {
    ledRojoEstado = !ledRojoEstado;
    digitalWrite(LED_ROJO, ledRojoEstado ? HIGH : LOW);
    proximoParpadeo = ahora + 200UL;
  }

  if (ahora >= finFeedback) {
    feedbackActivo = false;
    feedbackRojo = false;
    ledRojoEstado = false;
    digitalWrite(LED_ROJO, LOW);
    digitalWrite(LED_VERDE, LOW);
    mostrarPantallaClave();
  }
}

// --------------------------------------------------
// 23. BORRAR Y DESACTIVAR
// --------------------------------------------------
void borrarClave() {
  longitudClave = 0;
  claveIntroducida[0] = '\0';
  mostrarPantallaClave();
}

void desactivarJuego() {
  estado = DESACTIVADO;
  feedbackActivo = false;
  tecladoHabilitado = false;

  noTone(BUZZER);
  digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_VERDE, HIGH);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CLAVE CORRECTA");
  lcd.setCursor(0, 1);
  lcd.print("SISTEMA OFF");

  tone(BUZZER, 2000, 900);
  Serial.println("SISTEMA DESACTIVADO");
}

// --------------------------------------------------
// 24. COMPROBAR CONTRASENA
// --------------------------------------------------
void comprobarClave() {
  if (longitudClave != 4) {
    iniciarFeedback("FALTAN DIGITOS", "INTRODUCE 4 NUM", 1000UL, false);
    return;
  }

  if (strcmp(claveIntroducida, PASSWORD) == 0) {
    desactivarJuego();
  } else {
    longitudClave = 0;
    claveIntroducida[0] = '\0';

    iniciarFeedback("CLAVE INCORRECTA", "INTENTA DE NUEVO", 1500UL, true);
    Serial.println("CONTRASENA INCORRECTA");
  }
}

// --------------------------------------------------
// 25. PROCESAR TECLADO
// --------------------------------------------------
void procesarTecla(char tecla) {
  if (tecla >= '0' && tecla <= '9') {
    if (longitudClave < 4) {
      claveIntroducida[longitudClave] = tecla;
      longitudClave++;
      claveIntroducida[longitudClave] = '\0';
      mostrarPantallaClave();
    }
  } else if (tecla == '*') {
    borrarClave();
  } else if (tecla == '#') {
    comprobarClave();
  }
}

// --------------------------------------------------
// 26. CRONOMETRO FISICO
// --------------------------------------------------
void actualizarCronometro(unsigned long ahora) {
  // No descontar el minuto fisico mientras se resuelven las pistas web.
  // La cuenta atras comienza cuando actualizarHabilitacionTeclado()
  // habilita el teclado y reinicia marcaSegundo.
  if (estado != JUGANDO || !tecladoHabilitado) {
    return;
  }

  unsigned long transcurridos = (ahora - marcaSegundo) / 1000UL;
  if (transcurridos == 0) {
    return;
  }

  marcaSegundo += transcurridos * 1000UL;

  if (transcurridos >= tiempoRestante) {
    tiempoRestante = 0;
  } else {
    tiempoRestante -= transcurridos;
  }

  actualizarDigitosTiempo();

  if (tiempoRestante == 0) {
    estado = TIEMPO_AGOTADO;
    feedbackActivo = false;
    tecladoHabilitado = false;

    noTone(BUZZER);
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_ROJO, LOW);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("TIEMPO AGOTADO");
    lcd.setCursor(0, 1);
    lcd.print("FIN DEL JUEGO");

    proximaAlarma = ahora;
    proximoParpadeo = ahora;
    ledRojoEstado = false;
    alternarAlarma = false;

    Serial.println("TIEMPO AGOTADO");
  }
}

// --------------------------------------------------
// 27. PITIDOS PROGRESIVOS
// --------------------------------------------------
void actualizarPitidos(unsigned long ahora) {
  if (estado != JUGANDO) {
    return;
  }

  unsigned long intervalo = obtenerIntervaloPitido();

  if (ahora - ultimoPitido >= intervalo) {
    tone(BUZZER, 1800, 90);
    ultimoPitido = ahora;
  }
}

// --------------------------------------------------
// 28. ALARMA FINAL
// --------------------------------------------------
void actualizarAlarma(unsigned long ahora) {
  if (estado != TIEMPO_AGOTADO) {
    return;
  }

  if (ahora >= proximoParpadeo) {
    ledRojoEstado = !ledRojoEstado;
    digitalWrite(LED_ROJO, ledRojoEstado ? HIGH : LOW);
    proximoParpadeo = ahora + 150UL;
  }

  if (ahora >= proximaAlarma) {
    if (alternarAlarma) {
      tone(BUZZER, 1700, 190);
    } else {
      tone(BUZZER, 1100, 190);
    }

    alternarAlarma = !alternarAlarma;
    proximaAlarma = ahora + 300UL;
  }
}

// --------------------------------------------------
// 29. SETUP
// --------------------------------------------------
void setup() {
  Serial.begin(9600);

  // Generar una nueva clave al arrancar.
  inicializarAleatoriedad();
  generarContrasena();

  pinMode(BUZZER, OUTPUT);
  noTone(BUZZER);

  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_VERDE, LOW);

  lcd.begin(16, 2);

  for (byte i = 0; i < 8; i++) {
    pinMode(segmentos[i], OUTPUT);
    digitalWrite(segmentos[i], LOW);
  }

  for (byte i = 0; i < 4; i++) {
    pinMode(digitos[i], OUTPUT);
    digitalWrite(digitos[i], LOW);
  }

  teclado.setDebounceTime(25);

  tiempoRestante = DURACION_INICIAL;
  actualizarDigitosTiempo();

  estado = ESPERANDO_INICIO;
  tecladoHabilitado = false;
  mostrarPantallaEspera();

  // Este mensaje exacto permite a la web reconocer V2.4.
  Serial.println("==============================");
  Serial.println("ESCAPE ROOM V2.4 PREPARADO");
  Serial.println("Enviar GETCODE para ver la clave");
  Serial.println("Enviar START para comenzar");
  Serial.println("==============================");
}

// --------------------------------------------------
// 30. LOOP PRINCIPAL
// --------------------------------------------------
void loop() {
  // Refrescar display continuamente.
  refrescarDisplay();

  // Procesar comandos USB en todos los estados.
  procesarComandoSerial();

  unsigned long ahora = millis();

  if (estado == ESPERANDO_INICIO) {
    return;
  }

  if (estado == JUGANDO) {
    actualizarCronometro(ahora);

    // Si el cronometro se agota, no continuar el juego.
    if (estado != JUGANDO) {
      return;
    }

    // Desbloquear automaticamente a los 60 segundos.
    actualizarHabilitacionTeclado(ahora);

    // Actualizar el feedback visual de errores.
    actualizarFeedback(ahora);

    // El teclado no se consulta hasta que Arduino lo habilita.
    if (tecladoHabilitado && !feedbackActivo) {
      char tecla = teclado.getKey();
      if (tecla) {
        procesarTecla(tecla);
      }
    }

    // Los pitidos progresivos siguen durante la investigacion
    // y tambien durante el feedback de error.
    actualizarPitidos(ahora);
  }

  if (estado == TIEMPO_AGOTADO) {
    actualizarAlarma(ahora);
  }
}
