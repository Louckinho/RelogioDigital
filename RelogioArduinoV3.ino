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

// --- VARIÁVEIS DA AUTOMAÇÃO ---
unsigned long ultimaSincronizacao = 0;
const unsigned long intervaloNTP = 86400000; // 24 horas em milissegundos
// ------------------------------

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
  delay(2000);

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
  // Verifica comandos Serial
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

        Serial.println("⌛ Iniciando sincronização NTP, aguarde...");
        bool ok = ajustarComNTP(param);

        if (ok) {
          Serial.println("🎉 Sincronização NTP concluída com sucesso!");
          // Se forçou na mão, reseta o cronômetro automático também
          ultimaSincronizacao = millis(); 
        } else {
          Serial.println("⚠️ Sincronização NTP falhou para todos os servidores.");
        }

        sincronizandoNTP = false;
      } else {
        Serial.println("⏳ Sincronização NTP já está em andamento...");
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

  // --- LÓGICA DE AUTOMAÇÃO (Executa a cada 24h de forma assíncrona) ---
  if (!sincronizandoNTP && (millis() - ultimaSincronizacao > intervaloNTP)) {
    Serial.println("\n[SISTEMA] Iniciando sincronização automática de 24h...");
    sincronizandoNTP = true;
    
    // Passamos o fuso "-3" fixo para a automação. Ajuste se necessário.
    if (ajustarComNTP("-3")) {
      Serial.println("[SISTEMA] Sincronização automática bem-sucedida!");
      ultimaSincronizacao = millis(); // Reseta para mais 24h
    } else {
      Serial.println("[SISTEMA] Falha na sincronização automática. Tentará novamente em 1 hora.");
      // Recua o cronômetro para tentar de novo em 1 hora (3.600.000 ms) em vez de 24h
      ultimaSincronizacao = millis() - intervaloNTP + 3600000; 
    }
    
    sincronizandoNTP = false;
  }
  // --------------------------------------------------------------------

  // Heartbeat e status (pausa durante sincronização NTP)
  if (!sincronizandoNTP) {
    if (millis() - heartbeatLast > 10000) {
      DateTime agora = rtc.now();
      Serial.printf("\n⏳ RTC está rodando: %02d:%02d:%02d\n", agora.hour(), agora.minute(), agora.second());
      Serial.println("📋 Comandos disponíveis:");
      Serial.println(" - ajustar : Ajuste manual da hora (formato AAAA-MM-DD HH:MM:SS)");
      Serial.println(" - ntp [fusoHorario] = [ex: ntp -3] -> Ajusta a hora via internet (Wi-Fi + NTP)");
      Serial.println(" - 0 a 15  : Ajusta brilho do display (0 mínimo, 15 máximo)");
      Serial.printf("🔌 Rede Wi-Fi para NTP: SSID='%s', Senha='***'\n\n", ssid);

      heartbeatLast = millis();
    }
  }

  // Alterna display entre hora e temperatura
  static uint8_t modo = 0;  // 0 = hora, 1 = temperatura
  static uint32_t ultimaTroca = 0;
  static char ultimaMensagem[32] = "";
  static bool primeiraVez = true;

  uint32_t tempoAtual = millis();
  uint32_t duracaoModo = (modo == 0) ? 15000 : 3000;

  if (tempoAtual - ultimaTroca > duracaoModo) {
    modo = (modo == 0) ? 1 : 0;
    ultimaTroca = tempoAtual;
    primeiraVez = true;
  }

  char novaMensagem[32];
  if (modo == 0) {
    DateTime now = rtc.now();
    sprintf(novaMensagem, "%02d:%02d", now.hour(), now.minute());
  } else {
    float tempC = rtc.getTemperature();
    sprintf(novaMensagem, "%.1f C", tempC);
  }

  if (strcmp(novaMensagem, ultimaMensagem) != 0 || primeiraVez) {
    strcpy(ultimaMensagem, novaMensagem);
    display.displayClear();
    display.displayText(ultimaMensagem, PA_CENTER, display.getSpeed(), display.getPause(), PA_PRINT, PA_NO_EFFECT);
    display.displayReset();
    primeiraVez = false;
  }

  display.displayAnimate();
}

// Ajuste manual da hora via Serial
void ajustarHoraViaSerial() {
  Serial.println("\n⏰ AJUSTE MANUAL DA HORA");
  Serial.println("Digite a hora no formato: AAAA-MM-DD HH:MM:SS");
  Serial.println("Exemplo: 2025-06-21 15:28:45");
  Serial.print(">> ");

  while (!Serial.available());

  String entrada = Serial.readStringUntil('\n');
  entrada.trim();

  int ano, mes, dia, hora, minuto, segundo;
  int res = sscanf(entrada.c_str(), "%d-%d-%d %d:%d:%d", &ano, &mes, &dia, &hora, &minuto, &segundo);

  if (res == 6) {
    rtc.adjust(DateTime(ano, mes, dia, hora, minuto, segundo));
    Serial.println("✅ Hora ajustada com sucesso!");
    Serial.printf("Nova hora: %04d-%02d-%02d %02d:%02d:%02d\n", ano, mes, dia, hora, minuto, segundo);
  } else {
    Serial.println("❌ Formato inválido! Digite novamente o comando 'ajustar'.");
  }

  while (Serial.available()) Serial.read();
}

// Ajusta brilho e mostra mensagem Serial
void ajustarBrilho(int brilho) {
  if (brilho >= 0 && brilho <= 15) {
    display.setIntensity(brilho);
    Serial.printf("💡 Brilho ajustado para: %d\n", brilho);
  } else {
    Serial.println("❌ Comando inválido. Use 'ajustar', 'ntp' ou número de brilho (0-15).");
  }
}

// Ajusta hora via NTP com múltiplos servidores e offset de fuso
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
    Serial.println("\n❌ Falha ao conectar ao Wi-Fi!");
    return false;
  }

  Serial.println("\n✅ Wi-Fi conectado!");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  int offsetHoras = 0;
  if (param.length() > 0) {
    offsetHoras = param.toInt();
  }
  int offsetSegundos = offsetHoras * 3600;

  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP);

  bool sucesso = false;

  for (int i = 0; i < ntpServersCount; i++) {
    Serial.printf("⏳ Tentando servidor NTP: %s\n", ntpServers[i]);
    timeClient.setPoolServerName(ntpServers[i]);
    timeClient.setTimeOffset(offsetSegundos);
    timeClient.begin();

    unsigned long startNtp = millis();
    bool updated = false;

    // Aguarda até 5 segundos pela resposta do servidor NTP
    while (millis() - startNtp < 5000) {
      if (timeClient.update()) {
        updated = true;
        break;
      }
      delay(100);
    }

    if (updated) {
      Serial.println("🕒 Hora NTP recebida com sucesso.");
      time_t rawTime = timeClient.getEpochTime();
      struct tm* tmStruct = localtime(&rawTime);
      DateTime agora = DateTime(1900 + tmStruct->tm_year, 1 + tmStruct->tm_mon,
                                tmStruct->tm_mday, tmStruct->tm_hour,
                                tmStruct->tm_min, tmStruct->tm_sec);
      rtc.adjust(agora);
      Serial.printf("✅ RTC ajustado para: %04d-%02d-%02d %02d:%02d:%02d\n",
                    agora.year(), agora.month(), agora.day(), agora.hour(), agora.minute(), agora.second());

      sucesso = true;
      timeClient.end();
      break;
    } else {
      Serial.println("❌ Falha ao obter hora deste servidor.");
      timeClient.end();
    }
  }

  WiFi.disconnect(true); // Desliga o rádio Wi-Fi para poupar energia térmica e elétrica

  return sucesso;
}
