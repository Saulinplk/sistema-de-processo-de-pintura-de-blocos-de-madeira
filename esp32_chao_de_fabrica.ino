

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <DHT.h>

// ============================================================================
// CONFIGURACAO DE PINOS
// ============================================================================
#define PIN_TRIG         5
#define PIN_ECHO        18
#define PIN_DHT          4
#define PIN_LDR          1
#define PIN_PIR          6

#define PIN_LED_VERDE    7
#define PIN_LED_RGB_R   16

#define DHT_TYPE        DHT22

// ============================================================================
// PARAMETROS DO TANQUE
// ============================================================================
#define DIST_TANQUE_VAZIO_CM   30.0
#define DIST_TANQUE_CHEIO_CM    2.0
#define NIVEL_ALERTA_PCT       20.0

// ============================================================================
// CONFIGURACAO ESPNOW
// ============================================================================
// >>> TROQUE pelo MAC REAL do ESP32 RECEPTOR (monitoramento).
//     Descubra rodando Serial.println(WiFi.macAddress()) no receptor.
uint8_t macReceptor[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };

// >>> Canal Wi-Fi do receptor. Ele informa no Serial:
//     ">>> Informe ao ESP32 chao de fabrica: Canal WiFi = 11 <<<"
#define CANAL_ESPNOW   11

// ============================================================================
// ESTRUTURA DO PACOTE
// ============================================================================
typedef struct __attribute__((packed)) {
  float    nivel_tinta;
  float    temperatura;
  float    umidade;
  uint16_t luminosidade;
  uint8_t  presenca;
  uint32_t timestamp;
} PacoteSensores;

PacoteSensores pacote;

// ============================================================================
// VARIAVEIS GLOBAIS
// ============================================================================
DHT dht(PIN_DHT, DHT_TYPE);

unsigned long ultimaLeitura = 0;
const unsigned long INTERVALO_MS = 2000;

volatile bool ultimoEnvioOk = false;
bool alertaTinta = false;

// ============================================================================
// CALLBACK DE ENVIO
// ============================================================================
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  ultimoEnvioOk = (status == ESP_NOW_SEND_SUCCESS);

  if (ultimoEnvioOk) {
    Serial.printf(
      "Pacote enviado com sucesso: {nivel=%.1f%%, temp=%.1fC, umidade=%.1f%%, luz=%u, presenca=%u}\n",
      pacote.nivel_tinta,
      pacote.temperatura,
      pacote.umidade,
      pacote.luminosidade,
      pacote.presenca
    );
  } else {
    Serial.println("Falha no envio ESPNOW");
  }
}

// ============================================================================
// LEITURA DO NIVEL DO TANQUE
// ============================================================================
float lerNivelTanque() {

  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);

  digitalWrite(PIN_TRIG, LOW);

  long duracao = pulseIn(PIN_ECHO, HIGH, 30000);

  if (duracao == 0) {
    return -1.0;
  }

  float distancia = (duracao * 0.0343) / 2.0;

  float nivel =
    ((DIST_TANQUE_VAZIO_CM - distancia) /
    (DIST_TANQUE_VAZIO_CM - DIST_TANQUE_CHEIO_CM))
    * 100.0;

  if (nivel < 0) nivel = 0;
  if (nivel > 100) nivel = 100;

  return nivel;
}

// ============================================================================
// PISCAR LED
// ============================================================================
void piscarLed(uint8_t pino, uint8_t vezes, uint16_t ms) {

  for (uint8_t i = 0; i < vezes; i++) {

    digitalWrite(pino, HIGH);
    delay(ms);

    digitalWrite(pino, LOW);
    delay(ms);
  }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== ESP32-S3 CHAO DE FABRICA ===");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  pinMode(PIN_PIR, INPUT);

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_RGB_R, OUTPUT);

  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_RGB_R, LOW);

  analogReadResolution(12);

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Fixa o canal no mesmo do receptor (obrigatorio para ESPNOW funcionar).
  esp_wifi_set_channel(CANAL_ESPNOW, WIFI_SECOND_CHAN_NONE);

  Serial.print("MAC deste ESP32: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("Canal ESPNOW fixado em: %d\n", CANAL_ESPNOW);

  // Inicializa ESPNOW
  if (esp_now_init() != ESP_OK) {

    Serial.println("ERRO ao inicializar ESPNOW!");

    while (true) {
      piscarLed(PIN_LED_RGB_R, 1, 200);
    }
  }

  Serial.println("ESPNOW inicializado.");

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};

  memcpy(peerInfo.peer_addr, macReceptor, 6);
  peerInfo.channel = CANAL_ESPNOW;   // mesmo canal do receptor
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {

    Serial.println("ERRO ao adicionar peer!");

    while (true) {
      piscarLed(PIN_LED_RGB_R, 1, 500);
    }
  }

  Serial.println("Peer registrado. Sistema pronto.\n");

  digitalWrite(PIN_LED_VERDE, HIGH);
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {

  unsigned long agora = millis();

  if (agora - ultimaLeitura < INTERVALO_MS) {
    return;
  }

  ultimaLeitura = agora;

  // ----------------------------------------------------
  // LEITURA DOS SENSORES
  // ----------------------------------------------------
  float nivel = lerNivelTanque();

  float temp = dht.readTemperature();
  float umid = dht.readHumidity();

  uint16_t luz = analogRead(PIN_LDR);

  uint8_t pres = digitalRead(PIN_PIR);

  if (isnan(temp)) temp = 0.0;
  if (isnan(umid)) umid = 0.0;
  if (nivel < 0) nivel = 0.0;

  // ----------------------------------------------------
  // MONTA O PACOTE
  // ----------------------------------------------------
  pacote.nivel_tinta = nivel;
  pacote.temperatura = temp;
  pacote.umidade = umid;
  pacote.luminosidade = luz;
  pacote.presenca = pres;
  pacote.timestamp = agora;

  // ----------------------------------------------------
  // SERIAL
  // ----------------------------------------------------
  Serial.println("------------------------------------------");

  Serial.printf("Nivel do tanque: %.1f%%\n", nivel);
  Serial.printf("Temperatura: %.1f C | Umidade: %.1f%%\n", temp, umid);
  Serial.printf("Luminosidade: %u\n", luz);

  Serial.println(
    pres ?
    "Presenca detectada" :
    "Sem presenca"
  );

  // ----------------------------------------------------
  // ALERTA DE NIVEL BAIXO
  // ----------------------------------------------------
  alertaTinta = (nivel < NIVEL_ALERTA_PCT);

  if (alertaTinta) {

    digitalWrite(PIN_LED_VERDE, LOW);
    digitalWrite(PIN_LED_RGB_R, HIGH);

    Serial.println(">> Alerta! Nivel de tinta baixo.");
    Serial.println(">> Estado: Alerta - verificar tanque de tinta");

  } else {

    digitalWrite(PIN_LED_RGB_R, LOW);
    digitalWrite(PIN_LED_VERDE, HIGH);

    Serial.println(">> Estado: Operacao normal");
  }

  // ----------------------------------------------------
  // ENVIO ESPNOW
  // ----------------------------------------------------
  esp_err_t resultado =
      esp_now_send(
        macReceptor,
        (uint8_t*)&pacote,
        sizeof(pacote)
      );

  if (resultado != ESP_OK) {

    Serial.println("Falha no envio ESPNOW");

    piscarLed(PIN_LED_RGB_R, 2, 80);

  } else {

    if (!alertaTinta) {

      digitalWrite(PIN_LED_VERDE, LOW);
      delay(60);
      digitalWrite(PIN_LED_VERDE, HIGH);
    }
  }
}
