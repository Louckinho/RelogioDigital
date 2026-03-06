#include <Wire.h>
#include <RTClib.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <ESP8266WiFi.h>       // Ajuste se usar ESP32
#include <WiFiUdp.h>
#include <NTPClient.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CS_PIN D6

// Wi-Fi dados da rede oculta
const char* ssid = "Loky";
const char* password = "8395012andre";

RTC_DS3231 rtc;
MD_Parola display = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

const char* ntpServers[] = {
  "pool.ntp.org",
  "time.nist.gov",
  "time.google.com",
  "time.windows.com",
  "time.apple.com",
  "ntp.ubuntu.com",
  "time.cloudflare.com",
  "129.6.15.28",      // time-a.nist.gov IP direto
  "216.239.35.0",     // time.google.com IP (exemplo)
  "132.163.96.1"      // ntp1.jst.mfeed.ad.jp (exemplo)
};
const int ntpServersCount = sizeof(ntpServers) / sizeof(ntpServers[0]);

// Variáveis globais para controle
unsigned long heartbeatLast = 0;
bool sincronizandoNTP = false;

// Variáveis da Automação de 24h
unsigned long ultimaSincronizacao = 0;
const unsigned long intervaloNTP = 86400000; // 24 horas em milissegundos

// Array com os dias da semana em Português (0 = Domingo, 6 = Sábado)
const char diasDaSemana[7][4] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};

// Protótipos
void ajustarHoraViaSerial();
bool ajustarComNTP(String param = "");
void ajustarBrilho(int brilho);

void setup() {
  Serial.begin(115200);
  Wire.begin(D2, D1);  // SDA = D2, SCL = D1 no NodeMCU

  display.begin();
  display.setIntensity(1);
  display.setSpeed(80);
  display.setPause(1000);
  display.displayClear();

  if (!rtc.begin()) {
    Serial.println("RTC não encontrado!");
    while (1);
  }
  delay(2000); // Dá um fôlego para os capacitores do RTC carregarem no boot

  if (rtc.lostPower()) {
    Serial.println("RTC perdeu energia. Ajustando para hora do PC...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("✅ RTC ajustado com hora do computador.");
  }

  Serial.println("🕹 Digite 'ajustar' no console para configurar a hora manualmente.");
  Serial.println("🕹 Digite 'ntp' seguido do offset de horário (ex: ntp -3 para GMT-3).");
  Serial.println("🕹 Digite um número de 0 a 15 para ajustar o brilho do display.");
}

void loop() {
  // 1. VERIFICA COMANDOS MANUAIS (Monitor Serial)
  if (Serial.available()) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();

    if (comando.startsWith("ntp")) {
      if (!sincronizandoNTP) {
        sincronizandoNTP = true;

        String param = "";
        int espacoIndex = comando.indexOf(' ');
        if (espacoIndex != -1) {
          param = comando.substring(espacoIndex + 1);
          param.trim();
        }

        Serial.println("⌛ Iniciando sincronização NTP manual...");
        if (ajustarComNTP(param)) {
          Serial.println("🎉 Sincronização NTP concluída!");
          ultimaSincronizacao = millis(); // Reseta o cronômetro automático
        } else {
          Serial.println("⚠️ Falha na sincronização NTP.");
        }
        sincronizandoNTP = false;
      }
    }
    else if (comando == "ajustar") {
      ajustarHoraViaSerial();
    }
    else {
      int valor = comando.toInt();
      ajustarBrilho(valor);
    }
    while (Serial.available()) Serial.read();  // limpa buffer
  }

  // 2. AUTOMAÇÃO: SINCRONIZAÇÃO NTP A CADA 24 HORAS
  if (!sincronizandoNTP && (millis() - ultimaSincronizacao > intervaloNTP)) {
    Serial.println("\n[SISTEMA] Iniciando sincronização automática de 24h...");
    sincronizandoNTP = true;
    
    if (ajustarComNTP("-3")) { // Fuso GMT-3 fixo para o automático
      Serial.println("[SISTEMA] Sincronização automática bem-sucedida!");
      ultimaSincronizacao = millis();
    } else {
      Serial.println("[SISTEMA] Falha. Tentará novamente em 1 hora.");
      ultimaSincronizacao = millis() - intervaloNTP + 3600000; // Recua 1 hora
    }
    sincronizandoNTP = false;
  }

  // 3. TELEMETRIA SERIAL (HEARTBEAT)
  if (!sincronizandoNTP) {
    if (millis() - heartbeatLast > 10000) {
      DateTime agora = rtc.now();
      Serial.printf("\n⏳ RTC rodando: %02d:%02d:%02d | Data: %s %02d/%02d/%04d\n", 
                    agora.hour(), agora.minute(), agora.second(), 
                    diasDaSemana[agora.dayOfTheWeek()], agora.day(), agora.month(), agora.year());
      heartbeatLast = millis();
    }
  }

  // 4. MÁQUINA DE ESTADOS DO DISPLAY (Hora <-> Data)
  static uint8_t modo = 0;  // 0 = Hora, 1 = Data ("QUI 05")
  static uint32_t ultimaTroca = 0;
  static char ultimaMensagem[32] = "";
  static bool primeiraVez = true;

  uint32_t tempoAtual = millis();
  uint32_t duracaoModo = (modo == 0) ? 15000 : 4000; // 15s para hora, 4s para data

  if (tempoAtual - ultimaTroca > duracaoModo) {
    modo = (modo == 0) ? 1 : 0;
    ultimaTroca = tempoAtual;
    primeiraVez = true;
  }

  char novaMensagem[32];
  DateTime now = rtc.now(); // Puxa o pacote de dados do chip DS3231

  if (modo == 0) {
    // Renderiza a Hora (Ex: 22:50)
    sprintf(novaMensagem, "%02d:%02d", now.hour(), now.minute());
  } else {
    // Renderiza o Dia da Semana e o Dia do Mês (Ex: QUI 05)
    int diaSemanaInt = now.dayOfTheWeek();
    sprintf(novaMensagem, "%s %02d", diasDaSemana[diaSemanaInt], now.day());
  }

  // Só aciona o barramento SPI se a mensagem mudou (economiza CPU e evita LEDs piscando)
  if (strcmp(novaMensagem, ultimaMensagem) != 0 || primeiraVez) {
    strcpy(ultimaMensagem, novaMensagem);
    display.displayClear();
    display.displayText(ultimaMensagem, PA_CENTER, display.getSpeed(), display.getPause(), PA_PRINT, PA_NO_EFFECT);
    display.displayReset();
    primeiraVez = false;
  }

  display.displayAnimate();
}

// ======================================================================
// FUNÇÕES AUXILIARES
// ======================================================================

void ajustarHoraViaSerial() {
  Serial.println("\n⏰ AJUSTE MANUAL DA HORA");
  Serial.println("Exemplo: 2026-03-05 22:50:00");
  Serial.print(">> ");

  while (!Serial.available());
  String entrada = Serial.readStringUntil('\n');
  entrada.trim();

  int ano, mes, dia, hora, minuto, segundo;
  int res = sscanf(entrada.c_str(), "%d-%d-%d %d:%d:%d", &ano, &mes, &dia, &hora, &minuto, &segundo);

  if (res == 6) {
    rtc.adjust(DateTime(ano, mes, dia, hora, minuto, segundo));
    Serial.println("✅ Hora ajustada com sucesso!");
  } else {
    Serial.println("❌ Formato inválido!");
  }
  while (Serial.available()) Serial.read();
}

void ajustarBrilho(int brilho) {
  if (brilho >= 0 && brilho <= 15) {
    display.setIntensity(brilho);
    Serial.printf("💡 Brilho ajustado para: %d\n", brilho);
  }
}

bool ajustarComNTP(String param) {
  Serial.println("🌐 Conectando ao Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password, 0, nullptr, true);

  unsigned long startWiFi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWiFi < 10000) {
    delay(100);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ Falha de conexão Wi-Fi!");
    return false;
  }

  Serial.print("\n✅ IP: "); Serial.println(WiFi.localIP());

  int offsetHoras = param.length() > 0 ? param.toInt() : 0;
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP);
  bool sucesso = false;

  for (int i = 0; i < ntpServersCount; i++) {
    timeClient.setPoolServerName(ntpServers[i]);
    timeClient.setTimeOffset(offsetHoras * 3600);
    timeClient.begin();

    unsigned long startNtp = millis();
    bool updated = false;

    while (millis() - startNtp < 5000) {
      if (timeClient.update()) {
        updated = true;
        break;
      }
      delay(100);
    }

    if (updated) {
      time_t rawTime = timeClient.getEpochTime();
      struct tm* tmStruct = localtime(&rawTime);
      rtc.adjust(DateTime(1900 + tmStruct->tm_year, 1 + tmStruct->tm_mon,
                          tmStruct->tm_mday, tmStruct->tm_hour,
                          tmStruct->tm_min, tmStruct->tm_sec));
      sucesso = true;
      timeClient.end();
      break;
    }
    timeClient.end();
  }

  WiFi.disconnect(true); // Isola o rádio para eficiência térmica/elétrica
  return sucesso;
}
