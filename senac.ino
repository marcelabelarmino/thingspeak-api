#include <WiFi.h>
#include <PubSubClient.h>

// ===== CONFIGURAÇÕES WIFI =====
// SSID e senha da rede Wi-Fi à qual o dispositivo deve se conectar.
const char* SSID_WIFI     = "MADU";
const char* SENHA_WIFI    = "Zuri@2025"; 

// ===== THINGSPEAK MQTT =====
// Credenciais e informações para publicar via MQTT no ThingSpeak (Device -> MQTT -> seu device)
const char* ID_CLIENTE_MQTT = "PScoER8sICcdOAYpJAIELgU";  // exemplo: "channels/3093339/publish/XYZ"
const char* USUARIO_MQTT    = "PScoER8sICcdOAYpJAIELgU"; // normalmente ID curto fornecido pela plataforma
const char* SENHA_MQTT      = "+P+jyt101yOMGlFhEbqXhtGS";   // senha/token MQTT
const char* CORRETOR_MQTT   = "mqtt3.thingspeak.com";   // broker MQTT do ThingSpeak (ou outro)
const uint16_t PORTA_MQTT   = 1883; // porta sem TLS; usar 8883 para TLS/SSL

// TOPICO base para publicar (string construída dinamicamente)
String TOPICO_MQTT = String("channels/") + "3181195" + "/publish";

// ====== LIMITES / FILTRO ======
// Limites operacionais para acionar regras/alertas
float TEMP_MAXIMA       = 30.0f;  // temperatura máxima aceitável em °C
float UMID_MINIMA       = 60.0f;  // umidade mínima aceitável em %
bool  PUBLICAR_QUANDO_OK = true;  // controla quando publicar (apenas quando OK ou quando fora)

// ====== AMOSTRAGEM / ENVIO ======
// Número de amostras para calcular média antes de publicar
const uint8_t  N_AMOSTRAS   = 5;        // média de 5 amostras
// Intervalos de tempo (em ms)
const unsigned long AMOSTRA_MS   = 2000;   // intervalo entre leituras (2 s)
const unsigned long PUBLICAR_MS  = 20000;  // intervalo mínimo entre publicações (>=15s exigido pelo ThingSpeak; aqui 20s)

// ====== FAIL-SAFE: BUFFER OFFLINE ======
// Estrutura para guardar pacotes de média quando não for possível publicar (fila circular)
struct PacoteMedia {
  float t, h;         // temperatura e umidade médias
  unsigned long ts;   // timestamp relativo (millis()) quando o pacote foi criado
};

const uint16_t CAPACIDADE_FILA_OFFLINE = 100;  // capacidade máxima do buffer offline
PacoteMedia filaOffline[CAPACIDADE_FILA_OFFLINE];
uint16_t cabecaFila = 0, caudaFila = 0;        // índices da fila circular

// funções utilitárias para fila
bool filaVazia() { return cabecaFila == caudaFila; }
bool filaCheia() { return (uint16_t)((cabecaFila + 1) % CAPACIDADE_FILA_OFFLINE) == caudaFila; }

// Enfileira uma média na fila offline
bool enfileirarMedia(float t, float h) {
  if (filaCheia()) {
    // se estiver cheia, descarta o mais antigo (policy FIFO com descarte antigo)
    caudaFila = (caudaFila + 1) % CAPACIDADE_FILA_OFFLINE;
  }
  // coloca o pacote na posição cabecaFila e avança
  filaOffline[cabecaFila] = { t, h, millis() };
  cabecaFila = (cabecaFila + 1) % CAPACIDADE_FILA_OFFLINE;
  return true;
}

// Desenfileira (pega o pacote mais antigo) da fila offline
bool desenfileirarMedia(PacoteMedia &pkt) {
  if (filaVazia()) return false;
  pkt = filaOffline[caudaFila];
  caudaFila = (caudaFila + 1) % CAPACIDADE_FILA_OFFLINE;
  return true;
}

// ====== WATCHDOG / DEADLINE ======
// Se USE_WDT estiver definido, inclua e configure watchdog (proteção contra travamentos)
#if USE_WDT
  #include "esp_task_wdt.h"
  const int WDT_TIMEOUT_S = 10;  // timeout em segundos
#endif

// Tempo máximo tolerado sem publicar antes de considerar restart como medida de segurança
const unsigned long PRAZO_PUBLICACAO_MS = 30UL * 60UL * 1000UL; // 30 minutos
unsigned long ultimaPublicacaoComSucesso = 0; // armazena último millis() com publish bem-sucedido

// ====== ATUADOR ======
#define PINO_RELE 2       // pino de controle de um relé (ativo HIGH/LOW depende do hardware)
bool regraAtiva = false;  // estado da regra/atuador
const float HISTERSE_TEMP = 0.5f; // histerese para evitar oscillação (temperatura)
const float HISTERSE_UMID = 2.0f; // histerese para umidade

// ====== ESTADO ======
WiFiClient clienteWifi;           // cliente TCP para o WiFi (usado pelo PubSubClient)
PubSubClient clienteMqtt(clienteWifi); // cliente MQTT que usa o cliente WiFi
unsigned long ultimaAmostra   = 0; // timestamp da última leitura
unsigned long ultimaPublicacao = 0; // timestamp da última tentativa de publicação

// Backoff para reconexão (incremental para evitar muitos retries)
unsigned long proximaTentativaWifiMs = 0;
unsigned long proximaTentativaMqttMs = 0;
unsigned long backoffWifiMs = 1000;  // inicial 1s
unsigned long backoffMqttMs = 1000;  // inicial 1s

// Limita o backoff máximo para 30000 ms (30s)
unsigned long limitarBackoff(unsigned long v) { return v > 30000 ? 30000 : v; }

// ====== SIMULAÇÃO (trocar quando tiver sensores reais) ======
// Funções que simulam leituras (para desenvolvimento sem hardware)
// Quando tiver o sensor real substitua por leitura do sensor (DHT, SHT, ADC, etc.)
float lerTemperaturaSim() { 
  return 20 + (esp_random() % 300) / 10.0;  // retorna 20.0 a 50.0 °C aproximado
}

float lerUmidadeSim() { 
  return 30 + (esp_random() % 600) / 10.0;  // retorna 30.0 a 90.0 % aproximado
}

// ====== SERIAL ======
// Inicializa a porta serial para debug
void iniciarSerial() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  // Espera um curto período até que Serial esteja pronta (em alguns boards isso é necessário)
  while (!Serial && (millis() - t0 < 1500)) { delay(10); }
  Serial.println("\n[BOOT] Serial iniciado.");
}

// ====== WIFI ======
// Garante que o dispositivo esteja conectado ao WiFi; faz tentativas e aplica backoff
void garantirWiFi() {
  if (WiFi.status() == WL_CONNECTED) return; // já conectado

  unsigned long agora = millis();
  if (agora < proximaTentativaWifiMs) return; // ainda no backoff

  Serial.printf("[WiFi] Conectando a \"%s\"...\n", SSID_WIFI);
  WiFi.begin(SSID_WIFI, SENHA_WIFI);

  int tentativas = 0;
  // loop de tentativa curto (20 * 250ms = ~5s)
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(250);
    tentativas++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    // Conexão bem-sucedida: imprime IP e RSSI e reseta backoff
    Serial.printf("[WiFi] Conectado | IP: %s | RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    backoffWifiMs = 1000; // reseta backoff para próximo ciclo
  } else {
    // Falha em conectar: aumenta backoff (exponencial) e agenda próxima tentativa
    backoffWifiMs = limitarBackoff(backoffWifiMs * 2);
    proximaTentativaWifiMs = agora + backoffWifiMs;
    Serial.printf("[WiFi] Falha. Próxima tentativa em %lums.\n", backoffWifiMs);
  }
}

// ====== MQTT ======
// Garante que o cliente MQTT esteja conectado; define servidor e tenta conectar com credenciais
void garantirMQTT() {
  if (WiFi.status() != WL_CONNECTED) return; // precisa de WiFi
  if (clienteMqtt.connected()) return; // já conectado

  unsigned long agora = millis();
  if (agora < proximaTentativaMqttMs) return; // ainda no backoff MQTT

  clienteMqtt.setServer(CORRETOR_MQTT, PORTA_MQTT);
  Serial.printf("[MQTT] Conectando em %s:%u ... ", CORRETOR_MQTT, PORTA_MQTT);
  if (clienteMqtt.connect(ID_CLIENTE_MQTT, USUARIO_MQTT, SENHA_MQTT)) {
    Serial.println("OK");
    backoffMqttMs = 1000; // sucesso: reseta backoff
  } else {
    // Falha: imprime rc, aumenta backoff e agenda próxima tentativa
    Serial.printf("FALHA (rc=%d)\n", clienteMqtt.state());
    backoffMqttMs = limitarBackoff(backoffMqttMs * 2);
    proximaTentativaMqttMs = agora + backoffMqttMs;
    Serial.printf("[MQTT] Próxima tentativa em %lums.\n", backoffMqttMs);
  }
}

// ====== REGRA LOCAL (com histerese) ======
// Lógica local para acionar/desligar atuador (relé) baseada em limites e histerese
void atualizarRegra(float t, float h) {
  // decide se deve alarmar (fora dos limites definidos)
  bool deveAlarmar = (t > TEMP_MAXIMA) || (h < UMID_MINIMA);

  if (regraAtiva) {
    // se já ativa, só desativa se ambos voltarem abaixo/do acima com histerese
    if (t <= (TEMP_MAXIMA - HISTERSE_TEMP) && h >= (UMID_MINIMA + HISTERSE_UMID)) {
      regraAtiva = false;
      Serial.println("[REGRA] Normalizou. Atuador DESLIGADO.");
    }
  } else {
    // se não ativa, ativa quando limite for violado
    if (deveAlarmar) {
      regraAtiva = true;
      Serial.println("[REGRA] Limite violado. Atuador LIGADO.");
    }
  }
  // Escreve estado no pino do relé (HIGH/LOW conforme regraAtiva)
  digitalWrite(PINO_RELE, regraAtiva ? HIGH : LOW);
}

// ====== FILTRO ======
// Decide se a média coletada passa no filtro para ser publicada
bool passaFiltro(float mediaT, float mediaH) {
  // ok=true quando dentro dos limites
  bool ok = (mediaT <= TEMP_MAXIMA) && (mediaH >= UMID_MINIMA);
  // Se PUBLICAR_QUANDO_OK=true, publica apenas quando ok; caso contrário publica quando estiver fora
  return PUBLICAR_QUANDO_OK ? ok : !ok;
}

// ====== PUBLICAÇÃO (um pacote) ======
// Publica um pacote (média) via MQTT para o tópico configurado
bool publicarMedia(float t, float h) {
  if (WiFi.status() != WL_CONNECTED || !clienteMqtt.connected()) {
    Serial.println("[ENVIO] Sem Wi-Fi/MQTT. Não publicado.");
    return false;
  }
  // Garante janela mínima entre publicações (limitação do ThingSpeak)
  if (millis() - ultimaPublicacao < PUBLICAR_MS) {
    Serial.println("[ENVIO] Janela mínima ThingSpeak ainda não atingida.");
    return false;
  }

  // Forma o payload como query-string do ThingSpeak (field1, field2)
  char payload[100];
  snprintf(payload, sizeof(payload), "field1=%.2f&field2=%.2f&status=MQTTPUBLISH", h, t);
  // Publica no tópico MQTT (TOPICO_MQTT definido acima)
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

// ====== DRENAR FILA OFFLINE ======
// Tenta publicar pacotes que foram enfileirados enquanto o dispositivo estava offline
void tentarDrenarFilaOffline() {
  if (WiFi.status() != WL_CONNECTED || !clienteMqtt.connected()) return;
  if (millis() - ultimaPublicacao < PUBLICAR_MS) return; // respeita janela de publicação

  if (!filaVazia()) {
    PacoteMedia pkt;
    if (desenfileirarMedia(pkt)) {
      Serial.printf("[QUEUE] Tentando publicar pacote offline (%lu ms atrás): T=%.2f H=%.2f\n",
                    (unsigned long)(millis() - pkt.ts), pkt.t, pkt.h);
      publicarMedia(pkt.t, pkt.h);
    }
  }
}

// ====== ESTATÍSTICA: média de N amostras ======
float somaT = 0.0f, somaH = 0.0f;
uint8_t contadorAmostras = 0;

// Coleta leituras, acumula e avalia média a cada N_AMOSTRAS
void coletarEAvaliarMedia() {
  // Garante intervalo entre amostras
  if (millis() - ultimaAmostra < AMOSTRA_MS) return;
  ultimaAmostra = millis();

  // Aqui usa funções de simulação; substitua pelas leituras reais do sensor
  float t = lerTemperaturaSim();
  float h = lerUmidadeSim();
  somaT += t; somaH += h; contadorAmostras++;

  Serial.printf("[AMOSTRA] #%u | T=%.2f °C | H=%.2f %%\n", contadorAmostras, t, h);
  // Atualiza a regra local (atuador) com a leitura atual (evita ter que esperar a média)
  atualizarRegra(t, h);

  if (contadorAmostras >= N_AMOSTRAS) {
    // Calcula média quando acumulou N_AMOSTRAS
    float mediaT = somaT / contadorAmostras;
    float mediaH = somaH / contadorAmostras;
    Serial.printf("[ESTAT] Média(%u) -> T=%.2f | H=%.2f\n", contadorAmostras, mediaT, mediaH);

    // Reseta acumuladores para próxima janela
    somaT = somaH = 0.0f; 
    contadorAmostras = 0;

    // Aplica filtro: se não passar, não publica nem enfileira
    if (!passaFiltro(mediaT, mediaH)) {
      Serial.println("[FILTRO] Média reprovada. Não enviar / não enfileirar.");
      return;
    }

    // Tenta publicar; se falhar, salva na fila offline
    if (!publicarMedia(mediaT, mediaH)) {
      enfileirarMedia(mediaT, mediaH);
      Serial.printf("[QUEUE] Pacote OFFLINE enfileirado. Tam=%u\n",
                    (uint16_t)((cabecaFila + CAPACIDADE_FILA_OFFLINE - caudaFila) % CAPACIDADE_FILA_OFFLINE));
    }
  }
}

// ====== SETUP / LOOP ======
void setup() {
  iniciarSerial(); // inicia serial para debug
  pinMode(PINO_RELE, OUTPUT); // configura pino do relé como saída
  digitalWrite(PINO_RELE, LOW); // garante relé desligado no boot

#if USE_WDT
  // Se habilitado, inicializa watchdog (proteção de falhas)
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
  Serial.printf("[WDT] Habilitado (%ds).\n", WDT_TIMEOUT_S);
#endif

  randomSeed(esp_random()); // inicializa gerador aleatório (usado pelas simulações)
  WiFi.mode(WIFI_STA); // configura como estação WiFi (client)
  ultimaPublicacaoComSucesso = millis(); // marca tempo inicial
  garantirWiFi(); // tenta conectar WiFi no setup
  garantirMQTT(); // tenta conectar MQTT no setup

  Serial.println("[STATUS] Sistema pronto. Coletando amostras...");
}

void loop() {
#if USE_WDT
  esp_task_wdt_reset(); // reseta watchdog periodicamente
#endif

  garantirWiFi(); // mantém conexões (tenta reconectar se necessário)
  garantirMQTT();
  clienteMqtt.loop(); // processa callbacks MQTT e keepalive

  coletarEAvaliarMedia(); // coleta amostras e possivelmente publica
  tentarDrenarFilaOffline(); // tenta enviar pacotes pendentes

#if AUTO_RESTART
  // Estratégia opcional: reinicia dispositivo se ficar muito tempo sem publicar
  if (millis() - ultimaPublicacaoComSucesso > PRAZO_PUBLICACAO_MS) {
    Serial.println("[SAFETY] Sem publicações há muito tempo. Reiniciando firmware...");
    delay(500);
    ESP.restart();
  }
#endif

  delay(10); // pequeno delay para evitar loop muito apertado
}