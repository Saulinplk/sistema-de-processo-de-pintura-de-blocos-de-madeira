/*
 * ESP32 DE MONITORAMENTO - Camada de Serviço
 * -------------------------------------------------
 * Recebe dados via ESPNOW do ESP32 chão de fábrica,
 * exibe na matriz de LEDs e envia ao backend (Supabase).
 *
 * BIBLIOTECAS NECESSÁRIAS (instalar no Arduino IDE):
 *   - MD_Parola  (by majicDesigns)
 *   - MD_MAX72XX (by majicDesigns)
 *   - ArduinoJson (by Benoit Blanchon)
 *
 * PINAGEM:
 *   Matriz MAX7219: DIN=23, CLK=18, CS=5
 *   LED Verde     : GPIO 26
 *   LED Vermelho  : GPIO 27
 */

#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>

// =====================================================
//   CONFIGURAÇÕES — edite aqui antes de gravar
// =====================================================

// --- Rede Wi-Fi (necessário para enviar ao backend) ---
const char* SSID     = "SEU_SSID";
const char* PASSWORD = "SUA_SENHA";

// --- URL do backend Python (mesmo computador na rede) ---
// Exemplo: "http://192.168.1.100:8000/dados"
const char* BACKEND_URL = "http://SEU_IP:8000/dados";

// --- MAC do ESP32 Chão de Fábrica (para validar origem) ---
// Descubra rodando Serial.println(WiFi.macAddress()) no outro ESP32
uint8_t MAC_CHAO_FABRICA[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// =====================================================
//   PINOS
// =====================================================
#define LED_VERDE    26
#define LED_VERMELHO 27

// Matriz de LEDs MAX7219
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define NUM_DEVICES   4        // Quantas matrizes 8x8 em série
#define PIN_CS        5
#define PIN_CLK       18
#define PIN_DIN       23

MD_Parola matriz = MD_Parola(HARDWARE_TYPE, PIN_DIN, PIN_CLK, PIN_CS, NUM_DEVICES);

// =====================================================
//   ESTRUTURA DE DADOS (deve ser idêntica ao transmissor)
// =====================================================
typedef struct __attribute__((packed)) {
  float nivel_tinta;     // %
  float temperatura;     // °C
  float umidade;         // %
  int   luminosidade;    // 0-4095
  int   presenca;        // 0 ou 1
  char  timestamp[25];   // ISO 8601
} DadosSensores;

DadosSensores dadosAtuais;
volatile bool novoDadoFlag = false;
unsigned long ultimaRecepcao  = 0;
unsigned long ultimaTrocaTela = 0;
int telaAtual = 0;

// Controle do piscar do LED verde ao receber
unsigned long tempoLedVerde = 0;
bool ledVerdeAceso = false;

// =====================================================
//   CALLBACK ESPNOW — executa ao receber pacote
// =====================================================
void IRAM_ATTR OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  // Valida MAC de origem
  bool macOk = true;
  for (int i = 0; i < 6; i++) {
    if (MAC_CHAO_FABRICA[i] != 0xFF && info->src_addr[i] != MAC_CHAO_FABRICA[i]) {
      macOk = false;
      break;
    }
  }
  if (!macOk) {
    Serial.println("[ESPNOW] Pacote ignorado: MAC desconhecido.");
    return;
  }

  if (len == sizeof(DadosSensores)) {
    memcpy(&dadosAtuais, data, sizeof(DadosSensores));
    novoDadoFlag  = true;
    ultimaRecepcao = millis();
  } else {
    Serial.printf("[ESPNOW] Pacote com tamanho inesperado: %d bytes\n", len);
  }
}

// =====================================================
//   EXIBIÇÃO NA MATRIZ
// =====================================================
char bufTela[32];

void prepararTextoTela(int tela) {
  switch (tela) {
    case 0:
      snprintf(bufTela, sizeof(bufTela), "NVL %.0f%%", dadosAtuais.nivel_tinta);
      break;
    case 1:
      snprintf(bufTela, sizeof(bufTela), "TMP %.1fC", dadosAtuais.temperatura);
      break;
    case 2:
      snprintf(bufTela, sizeof(bufTela), "UMD %.0f%%", dadosAtuais.umidade);
      break;
    case 3:
      snprintf(bufTela, sizeof(bufTela), "LUX %d", dadosAtuais.luminosidade);
      break;
    case 4:
      snprintf(bufTela, sizeof(bufTela), "PRS %s", dadosAtuais.presenca ? "ON" : "OFF");
      break;
  }
  const char* nomes[] = {"NVL", "TMP", "UMD", "LUX", "PRS"};
  Serial.printf("[TELA] -> %s  (%s)\n", nomes[tela], bufTela);
}

// =====================================================
//   ENVIAR DADOS AO BACKEND (Python + Supabase)
// =====================================================
void enviarAoBackend(DadosSensores& d) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi desconectado, dados não enviados.");
    return;
  }

  HTTPClient http;
  http.begin(BACKEND_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  // Monta JSON
  StaticJsonDocument<256> doc;
  doc["nivel_tinta"]  = d.nivel_tinta;
  doc["temperatura"]  = d.temperatura;
  doc["umidade"]      = d.umidade;
  doc["luminosidade"] = d.luminosidade;
  doc["presenca"]     = d.presenca;
  doc["timestamp"]    = d.timestamp;

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("[HTTP] Dados enviados com sucesso (HTTP %d)\n", httpCode);
  } else {
    Serial.printf("[HTTP] Falha no envio (HTTP %d)\n", httpCode);
  }
  http.end();
}

// =====================================================
//   SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 Monitoramento - Iniciando ===");

  // --- LEDs de status ---
  pinMode(LED_VERDE,    OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  digitalWrite(LED_VERDE,    LOW);
  digitalWrite(LED_VERMELHO, HIGH);  // vermelho: aguardando conexão

  // --- Matriz de LEDs ---
  matriz.begin();
  matriz.setIntensity(5);   // brilho 0-15
  matriz.displayClear();
  matriz.displayText("BOOT", PA_CENTER, 50, 1000, PA_PRINT, PA_NO_EFFECT);
  while (!matriz.displayAnimate());
  delay(500);

  // --- WiFi modo Station (obrigatório para ESPNOW + HTTP) ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("[WiFi] Conectando");
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Conectado! IP: %s  Canal: %d\n",
      WiFi.localIP().toString().c_str(), WiFi.channel());
    // *** IMPORTANTE: informe este canal ao ESP32 chão de fábrica ***
    Serial.printf(">>> Informe ao ESP32 chão de fábrica: Canal WiFi = %d <<<\n",
      WiFi.channel());
  } else {
    Serial.println("\n[WiFi] Falha na conexão. Continuando sem envio ao backend.");
  }

  // --- ESPNOW ---
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] ERRO ao inicializar!");
    matriz.displayText("ERR ESP", PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("[ESPNOW] Inicializado. Aguardando pacotes...");

  // --- Tela inicial ---
  matriz.displayText("AGUARD", PA_CENTER, 60, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);

  ultimaRecepcao  = millis();
  ultimaTrocaTela = millis();
  Serial.println("=== Sistema pronto ===\n");
}

// =====================================================
//   LOOP
// =====================================================
void loop() {
  unsigned long agora = millis();

  // ----- Timeout: sem dados há mais de 5 segundos -----
  if (agora - ultimaRecepcao > 5000) {
    digitalWrite(LED_VERDE,    LOW);
    digitalWrite(LED_VERMELHO, HIGH);

    if (matriz.displayAnimate()) {
      matriz.displayText("SEM DADOS", PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      Serial.println("[ALERTA] Timeout: sem dados ESPNOW há 5+ segundos.");
      delay(2000);
    }
    return;
  }

  // ----- LED verde: pisca 300ms ao receber dado -----
  if (ledVerdeAceso && (agora - tempoLedVerde > 300)) {
    ledVerdeAceso = false;
    digitalWrite(LED_VERDE, HIGH);  // mantém aceso depois do piscar
  }

  // ----- Novo dado chegou via ESPNOW -----
  if (novoDadoFlag) {
    novoDadoFlag = false;

    // Pisca LED verde
    digitalWrite(LED_VERDE,    LOW);
    digitalWrite(LED_VERMELHO, LOW);
    delay(50);
    digitalWrite(LED_VERDE, HIGH);
    ledVerdeAceso = true;
    tempoLedVerde = agora;

    // Log completo no Serial
    Serial.printf("[RX] nivel=%.1f%% temp=%.1fC umd=%.1f%% lux=%d prs=%d ts=%s\n",
      dadosAtuais.nivel_tinta, dadosAtuais.temperatura, dadosAtuais.umidade,
      dadosAtuais.luminosidade, dadosAtuais.presenca, dadosAtuais.timestamp);
    Serial.printf("[LED] VERDE ON – dados recebidos\n");

    // Envia ao backend
    enviarAoBackend(dadosAtuais);
  }

  // ----- Troca de tela a cada 2 segundos -----
  if (agora - ultimaTrocaTela >= 2000) {
    ultimaTrocaTela = agora;
    telaAtual = (telaAtual + 1) % 5;
    prepararTextoTela(telaAtual);
    matriz.displayText(bufTela, PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  }

  // ----- Anima a matriz -----
  matriz.displayAnimate();
}
