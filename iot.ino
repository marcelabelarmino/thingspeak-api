#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"
#include "Adafruit_Sensor.h" 

#define PINO_DHT 5       
#define TIPO_DHT DHT11   
DHT dht(PINO_DHT, TIPO_DHT); 

const char* SSID_WIFI     = "NOME-REDE";
const char* SENHA_WIFI    = "SENHA-REDE"; 

const char* ID_CLIENTE_MQTT = "ID_CLIENTE_MQTT";  
const char* USUARIO_MQTT    = "USUARIO_MQTT"; 
const char* SENHA_MQTT      = "SENHA_MQTT";   
const char* CORRETOR_MQTT   = "mqtt3.thingspeak.com";   
const uint16_t PORTA_MQTT   = 1883; 

String TOPICO_MQTT = String("channels/") + "3181195" + "/publish";

const uint8_t  N_AMOSTRAS   = 5;        
const unsigned long AMOSTRA_MS   = 2000;   
const unsigned long PUBLICAR_MS  = 120000;  // AJUSTADO: 2 minutos (120 * 1000 ms)

struct PacoteMedia {
  float t, h;         
  unsigned long ts;   
};

const uint16_t CAPACIDADE_FILA_OFFLINE = 100;  
PacoteMedia filaOffline[CAPACIDADE_FILA_OFFLINE];
uint16_t cabecaFila = 0, caudaFila = 0;        

bool filaVazia() { return cabecaFila == caudaFila; }
bool filaCheia() { return (uint16_t)((cabecaFila + 1) % CAPACIDADE_FILA_OFFLINE) == caudaFila; }

bool enfileirarMedia(float t, float h) {
  if (filaCheia()) {
    caudaFila = (caudaFila + 1) % CAPACIDADE_FILA_OFFLINE;
  }
  filaOffline[cabecaFila] = { t, h, millis() };
  cabecaFila = (cabecaFila + 1) % CAPACIDADE_FILA_OFFLINE;
  return true;
}

bool desenfileirarMedia(PacoteMedia &pkt) {
  if (filaVazia()) return false;
  pkt = filaOffline[caudaFila];
  caudaFila = (caudaFila + 1) % CAPACIDADE_FILA_OFFLINE;
  return true;
}

#ifdef USE_WDT
  #include "esp_task_wdt.h"
  const int WDT_TIMEOUT_S = 10; 
#endif

const unsigned long PRAZO_PUBLICACAO_MS = 30UL * 60UL * 1000UL; 
unsigned long ultimaPublicacaoComSucesso = 0; 

#define PINO_RELE 2       

WiFiClient clienteWifi;           
PubSubClient clienteMqtt(clienteWifi); 
unsigned long ultimaAmostra   = 0; 
unsigned long ultimaPublicacao = 0; 

unsigned long proximaTentativaWifiMs = 0;
unsigned long proximaTentativaMqttMs = 0;
unsigned long backoffWifiMs = 1000; 
unsigned long backoffMqttMs = 1000; 

unsigned long limitarBackoff(unsigned long v) { return v > 30000 ? 30000 : v; }

float lerTemperaturaReal() { 
  return dht.readTemperature();
}

float lerUmidadeReal() { 
  return dht.readHumidity();
}

void iniciarSerial() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 1500)) { delay(10); }
  Serial.println("\n[BOOT] Serial iniciado.");
}

void garantirWiFi() {
  if (WiFi.status() == WL_CONNECTED) return; 

  unsigned long agora = millis();
  if (agora < proximaTentativaWifiMs) return; 

  Serial.printf("[WiFi] Conectando a \"%s\"...\n", SSID_WIFI);
  WiFi.begin(SSID_WIFI, SENHA_WIFI);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(250);
    tentativas++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Conectado | IP: %s | RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    backoffWifiMs = 1000; 
  } else {
    backoffWifiMs = limitarBackoff(backoffWifiMs * 2);
    proximaTentativaWifiMs = agora + backoffWifiMs;
    Serial.printf("[WiFi] Falha. Próxima tentativa em %lums.\n", backoffWifiMs);
  }
}

void garantirMQTT() {
  if (WiFi.status() != WL_CONNECTED) return; 
  if (clienteMqtt.connected()) return; 

  unsigned long agora = millis();
  if (agora < proximaTentativaMqttMs) return; 

  clienteMqtt.setServer(CORRETOR_MQTT, PORTA_MQTT);
  Serial.printf("[MQTT] Conectando em %s:%u ... ", CORRETOR_MQTT, PORTA_MQTT);
  if (clienteMqtt.connect(ID_CLIENTE_MQTT, USUARIO_MQTT, SENHA_MQTT)) {
    Serial.println("OK");
    backoffMqttMs = 1000; 
  } else {
    Serial.printf("FALHA (rc=%d)\n", clienteMqtt.state());
    backoffMqttMs = limitarBackoff(backoffMqttMs * 2);
    proximaTentativaMqttMs = agora + backoffMqttMs;
    Serial.printf("[MQTT] Próxima tentativa em %lums.\n", backoffMqttMs);
  }
}

void atualizarRegra(float t, float h) {
  // Lógica do atuador removida. O relé permanece LOW (desligado)
}

bool passaFiltro(float mediaT, float mediaH) {
  return true; 
}

bool publicarMedia(float t, float h) {
  if (WiFi.status() != WL_CONNECTED || !clienteMqtt.connected()) {
    Serial.println("[ENVIO] Sem Wi-Fi/MQTT. Não publicado.");
    return false;
  }
  if (millis() - ultimaPublicacao < PUBLICAR_MS) {
    Serial.println("[ENVIO] Janela mínima ThingSpeak ainda não atingida.");
    return false;
  }

  char payload[100];
  snprintf(payload, sizeof(payload), "field1=%.2f&field2=%.2f&status=MQTTPUBLISH", h, t);
  bool ok = clienteMqtt.publish(TOPICO_MQTT.c_str(), payload);
  if (ok) {
    ultimaPublicacao = millis();
    ultimaPublicacaoComSucesso = millis();
    Serial.printf("[ENVIO] OK -> T=%.2f | H=%.2f\n", t, h);
  } else {
    Serial.println("[ENVIO] FALHA no publish().");
  }
  return ok;
}

void tentarDrenarFilaOffline() {
  if (WiFi.status() != WL_CONNECTED || !clienteMqtt.connected()) return;
  if (millis() - ultimaPublicacao < PUBLICAR_MS) return; 

  if (!filaVazia()) {
    PacoteMedia pkt;
    if (desenfileirarMedia(pkt)) {
      Serial.printf("[QUEUE] Tentando publicar pacote offline (%lu ms atrás): T=%.2f H=%.2f\n",
                    (unsigned long)(millis() - pkt.ts), pkt.t, pkt.h);
      publicarMedia(pkt.t, pkt.h);
    }
  }
}

float somaT = 0.0f, somaH = 0.0f;
uint8_t contadorAmostras = 0;

void coletarEAvaliarMedia() {
  if (millis() - ultimaAmostra < AMOSTRA_MS) return;
  ultimaAmostra = millis();

  float t = lerTemperaturaReal();
  float h = lerUmidadeReal();

  if (isnan(t) || isnan(h)) {
    Serial.println("[AMOSTRA] Falha ao ler do sensor DHT! Descartando amostra.");
    return;
  }
  
  somaT += t; somaH += h; contadorAmostras++;
  Serial.printf("[AMOSTRA] #%u | T=%.2f °C | H=%.2f %%\n", contadorAmostras, t, h);
  
  atualizarRegra(t, h);

  if (contadorAmostras >= N_AMOSTRAS) {
    float mediaT = somaT / contadorAmostras;
    float mediaH = somaH / contadorAmostras;
    Serial.printf("[ESTAT] Média(%u) -> T=%.2f | H=%.2f\n", contadorAmostras, mediaT, mediaH);

    somaT = somaH = 0.0f; 
    contadorAmostras = 0;

    if (!passaFiltro(mediaT, mediaH)) {
      return; 
    }

    if (!publicarMedia(mediaT, mediaH)) {
      enfileirarMedia(mediaT, mediaH);
      Serial.printf("[QUEUE] Pacote OFFLINE enfileirado. Tam=%u\n",
                    (uint16_t)((cabecaFila + CAPACIDADE_FILA_OFFLINE - caudaFila) % CAPACIDADE_FILA_OFFLINE));
    }
  }
}

void setup() {
  iniciarSerial(); 
  pinMode(PINO_RELE, OUTPUT); 
  digitalWrite(PINO_RELE, LOW); 
  
  dht.begin();
  Serial.printf("[DHT] Sensor DHT11 iniciado no pino %d.\n", PINO_DHT);
  
#ifdef USE_WDT
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
  Serial.printf("[WDT] Habilitado (%ds).\n", WDT_TIMEOUT_S);
#endif

  WiFi.mode(WIFI_STA); 
  ultimaPublicacaoComSucesso = millis(); 
  garantirWiFi(); 
  garantirMQTT(); 

  Serial.println("[STATUS] Sistema pronto. Coletando amostras...");
}

void loop() {
#ifdef USE_WDT
  esp_task_wdt_reset(); 
#endif

  garantirWiFi(); 
  garantirMQTT();
  clienteMqtt.loop(); 

  coletarEAvaliarMedia(); 
  tentarDrenarFilaOffline(); 

#ifdef AUTO_RESTART
  if (millis() - ultimaPublicacaoComSucesso > PRAZO_PUBLICACAO_MS) {
    Serial.println("[SAFETY] Sem publicações há muito tempo. Reiniciando firmware...");
    delay(500);
    ESP.restart();
  }
#endif

  delay(10); 
}
