//Pierwsza finalna wersja v1.0 zaprezentowana na garażu złożoności 18.06.2026, posiada ręczne sterowanie (MANUAL), tryb automatyczny (4 fotorezystory) i tryb kalkulowany (obliczanie pozycji słońca na podstawie daty), który wymaga podania obecnego czasu.

#include <Servo.h> //Biblioteka do sterowania servo
#include <SolarCalculator.h> // Biblioteka do obliczania pozycji słońca na podstawie godziny i położenia geograficznego
#include <TimeLib.h> // Lepsza biblioteka do zarządzania czasem

// #include <LowPower.h> //Biblioteka która pozwala na wejście w tryb uśpienia, na razie zbedna

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//KOMENDY:
// MANUAL - przełącza w tryb manualny, wtedy nie rusza się sam z siebie, a tylko gdy rozkaże mu się za pomocą S1 i S2.
// AUTO - powraca do trybu auto.
// CALC - przełącza w tryb obliczeniowy (obliczenia kąta na podstawie daty)
// S1 kąt (0-180) - rusza serwem nr1 (tylko w Manual)
// S2 kąt (0 180) - rusza serwem nr2 (tylko w Manual)
// CZAS godzina:minuta dzień-miesiąc-rok - ustawia obecną datę. 
// FORMAT - pomocno komenda pokazujaca format czasu


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//TUTAJ RZECZY CO MOŻNA ZMIENIAĆ BY WIDZIEĆ EFEKT:
const int moveInterval = 25; // ms między krokami (1 stopien). Predkosc serwa. Wieksza liczba, wolniej sie obraca.
const float poprawka_s2 = 1;
const float poprawka_s3 = 1.1;
const float poprawka_s4 = 1; // poprawka na oko żeby rezystory działały tak samo. Mnoży zmierzoną wartośc napięcia na fotorezystorze.
const int srednie = 5; //ilość średnich pomiarów napięcia. Nie powinno miec znaczącej róznicy.
const int _delay = 1000; //Czas (ms) pomiędzy każdym cyklem. Aka wykonuje się pomiar, ruch, a potem odpoczywa przez np. 1000ms
const int poprawka_krzywy = -15; //WAŻNA, poprawka w stopniach spowodowana tym, że układ jest krzywy i sie rusza i nie jest idealnie wymierzony.
const float poprawka_kat = 0.2; //WAŻNA eksperymentalna poprawka w postaci poprawka_kat*kąt^3 (gdzie kąt jest od 0 do 1). Dla małych kątów wtedy jest prawie tak samo jak bez poprawki, a dla dużych kątów jest istotna poprawka. Można zmienić na zero jak będzie źle.

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Do sterowanie ręcznego/automatycznego/obliczanego. Z automatu jest tryb automatyczny.
bool manualMode = false;
bool calcMode = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Zmienne standardowe do obliczeń na podstawie daty.
double latitude = 50.029468; //Pozycja Krakowa
double longitude = 19.906156;
int time_zone = 2; // Strefa czasowa Kraków.
double elevation;
double azimuth;
int kierunek = 0; //W jakim kierunku chce odbic swiatlo slonca. 0 stopni to w kierunku północy

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


volatile bool buttonPressed = false; // Do działania przycisku
volatile unsigned long lastInterrupt = 0; // Do eliminacji zaklocen z serva i przycisku

const unsigned long holdTime = 1000; // 2 sekundy do trzymania przycisku
unsigned long buttonStart = 0;
bool buttonHandled = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//Definiuje tutaj które wejścia na arduino używam do czego
const int sensor_pin0 = A0;
const int sensor_pin1 = A1;
const int sensor_pin2 = A2;
const int sensor_pin3 = A3;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//Definiuje tutaj zmienne
int sensor1; //odczyt napięcia na fotorezystorze 1
int sensor2; //odczyt napięcia na fotorezystorze 2
int sensor3;
int sensor4;
float sensor2pop; //poprawka na fotorezystor 2, którą zrobiłem na podstawie tego, że sensor2 zawsze mniej zczytywał przy takich samych warunkach 
float sensor3pop; 
float sensor4pop;  
float roznica_y; //(s2+s4-s1-s3)
float roznica_x; //(s1+s2-s3-s4)
float suma; //s1+s2+s3+s4
float kat_y; //roznica_y/suma, wartosc od -1 do 1.
float kat_x; //roznica_x/suma


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// servo, tutaj definiuje zmienne dla serva. 
Servo serwo1;
Servo serwo2;
int pozycja_s1 = 90;
int pozycja_s2 = 90;
int target_s1 = 90;
int target_s2 = 90;
int zmiana;
int stara = 90;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void setup() {

  Serial.begin(9600);
  pinMode(2, INPUT_PULLUP); //przycisk
  attachInterrupt(digitalPinToInterrupt(2), buttonISR, FALLING);  //przycisk
  //SERVA PODPINAM OSOBNO W KAŻDYM CYKLU BO ZNACZĄCO MAJĄ WPŁYW NA POMIARY.

  // rusza serwa na pozycje początkową, kąty od 0 do 180, wiec 90 to środek.
  // serwo1.write(90);
  // serwo2.write(90);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// główna pętla, wykonuje się w kółko bez końca
void loop() {

  button(); //wykrywanie wcisniecia przycisku.

  if (!manualMode&&!calcMode){
  
    automatic(); // tryb automatyczny

  }



  if(!manualMode&&calcMode){

    calc(); //tryb kalkulowany 

  }
  // Funkcja do kalkulowania kątów i ruszania servem


  tryb(); //komendy i przełączanie trybu


  // Podczas testów odkryłem, że serva gdy są podłączone to biorą tak dużo prądu ze kompletnie rujnują pomiary napięcia.
  // Dlatego odłączam je i podłączam
  serwo1.attach(9);
  moveServoSlow(serwo1, target_s1, pozycja_s1);
  delay(100);
  serwo1.detach();
  
  serwo2.attach(10);
  moveServoSlow(serwo2, target_s2, pozycja_s2);  
  delay(100);
  serwo2.detach();


  // czekam (1s standardowo)
  delay(_delay);


  // tutaj kod był co nie ruszal servem gdy zmiana kata byla mała
  // if(abs(pozycja - stara) > 2){
  //     serwo.write(pozycja);
  //     stara = pozycja;
  // };

  // Tutaj był kod z oszczedzaniem energii, ale na razie nie jest to istotne.
  // odpinam servo by oszczędzać energię
  // serwo2.detach();
  
  // // usypiam układ na 2 sekundy
  // // for(int i = 0; i < 1; i++) {
  // //     LowPower.powerDown(SLEEP_2S, ADC_OFF, BOD_OFF);
  // // }
  
  // // podłączam servo z powrotem do pinu 9.
  // serwo2.attach(10);

}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



//komendy, przelaczanie trybow etc. Tutaj też jest manual.
void tryb(){
// Komendy są "S1 kat" i "S2 kąt" przykładowo "S1 50" - przesunie serwo1 na pozycje 50 stopni.
// Kąt oczywiście od 0 do 180.
 if (Serial.available()){

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
    //Włączenie wyłączenie trybu ręcznego.
    if (cmd.equalsIgnoreCase("MANUAL"))
    {
        manualMode = true;
        calcMode = false;
        Serial.println("Tryb ręczny");
        return;
    }

    if (cmd.equalsIgnoreCase("AUTO"))
    {
        manualMode = false;
        calcMode = false;
        Serial.println("Tryb automatyczny");
        return;
    }
    if (cmd.equalsIgnoreCase("CALC"))
    {
        manualMode = false;
        calcMode = true;
        Serial.println("Tryb obliczeniowy");
        time_t utc = now();
        printUTC(utc);
        return;
    } 
    // Komenda pokazująca format
    if(cmd=="FORMAT"){
        Serial.println("Format to: minuta:godzina dzień-miesiąc-rok");
    }
    else{
        czas(cmd);    
    }



  char Servoname[3];
  int angle;
  if(manualMode){
  if (sscanf(cmd.c_str(), "%2s %d", Servoname, &angle) ==2){
      angle = constrain(angle,0,180);

        if (strcmp(Servoname, "S1")==0){

          Serial.print("serwo 1: ");
          Serial.println(angle);        
          target_s1 = angle;
        }

        else if (strcmp(Servoname, "S2")==0){
          Serial.print("serwo 2: ");
          Serial.println(angle);
          target_s2 = angle;


        }
        else {
          Serial.println("Nieznane serwo");
        }}
    else{
      Serial.println("Błędna komenda");
    }  
    }
 }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Funkcja zczytująca czas z terminala.
void czas(String s) {
    s.trim();

  int hour, minute, day, month, year;

  int n = sscanf(
    s.c_str(),
    "CZAS %d:%d %d-%d-%d",
    &hour,
    &minute,
    &day,
    &month,
    &year
  );
  if (n == 5) {

    tmElements_t tm;

    tm.Second = 0;
    tm.Minute = minute;
    tm.Hour   = hour-time_zone;
    tm.Day    = day;
    tm.Month  = month;
    tm.Year   = CalendarYrToTm(year);

    setTime(makeTime(tm));

    Serial.println("Czas ustawiony.");
  }
  else {
    Serial.println("Niepoprawny format.");
  }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// pomocniczna funkcja, która wypisuje czas z UTC w bardziej zrozumiałym formacie.
void printUTC(time_t t)
{
  Serial.print(year(t));
  Serial.print("-");

  if (month(t) < 10) Serial.print('0');
  Serial.print(month(t));
  Serial.print("-");

  if (day(t) < 10) Serial.print('0');
  Serial.print(day(t));

  Serial.print(" ");

  if (hour(t) < 10) Serial.print('0');
  Serial.print(hour(t));
  Serial.print(":");

  if (minute(t) < 10) Serial.print('0');
  Serial.print(minute(t));
  Serial.print(":");

  if (second(t) < 10) Serial.print('0');
  Serial.print(second(t));

  Serial.println();
}  


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//przycisk
void button(){
      // wykrywanie nacisniecia przycisku w celu zmiany stanu.
  if (digitalRead(2) == LOW) {

      if (buttonStart == 0) {
          buttonStart = millis();
      }

      if (!buttonHandled &&
          millis() - buttonStart > holdTime) {

          buttonHandled = true;

          manualMode = false;

          if (!calcMode) {
              calcMode = true;
              Serial.println("Uruchomiono tryb obliczeniowy.");
          }
          else {
              calcMode = false;
              Serial.println("Uruchomiono tryb automatyczny.");
          }
      }
  }
  else {
      buttonStart = 0;
      buttonHandled = false;
  }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// TRYB AUTOMATYCZNY
void automatic(){
  //zczytywanie wartosci napiecia, obecnie 2 sensorki, liczy średnią kilku pomiarów np. 5 ustalone u góry.
  sensor1 = averageAnalogRead(sensor_pin0);
  sensor2 = averageAnalogRead(sensor_pin1);
  sensor3 = averageAnalogRead(sensor_pin2);
  sensor4 = averageAnalogRead(sensor_pin3);

  //poprawki, które estymowałem na oko, zależy od tego jak rezystory odstają dla identycznych warunków
  sensor2pop = sensor2*poprawka_s2;
  sensor3pop = sensor3*poprawka_s3;
  sensor4pop = sensor4*poprawka_s4;

  //Tutaj tylko wypisuje w konsoli dane zeby łatwiej było mi weryfikować co się dzieje, ten part jest zbędny do działania
  Serial.print("sensor 1: ");
  Serial.print(sensor1);
  Serial.print("  sensor 2: ");
  Serial.print(sensor2pop);
  Serial.print("  sensor 3: ");
  Serial.print(sensor3pop);
  Serial.print("  sensor 4: ");
  Serial.println(sensor4pop);

  //obliczenia, kat jako roznica przez sume 
  // wykonuje obliczenie nowej pozycji tylko jesli suma zczytanych napiec jest wieksza niz 20, żeby nie wykonywało zmiany pozycji mamy gdy słabe światło. 
  suma = sensor3pop+sensor4pop+sensor2pop + sensor1;  
  if(suma>30){
    roznica_x = (sensor1 + sensor2pop - sensor3pop - sensor4pop);
    roznica_y = -(sensor4pop + sensor2pop - sensor3pop - sensor1); 
    kat_y = (float)roznica_y/suma;
    kat_x = (float)roznica_x/suma; 
    
    target_s1=kat_y*45+45+poprawka_krzywy+poprawka_kat*45*kat_y*kat_y*kat_y;
    target_s1 = constrain(target_s1, 0, 90);
    target_s2=kat_x*90+90+poprawka_kat*45*kat_x*kat_x*kat_x;
    target_s2 = constrain(target_s2, 0, 180);     
  }

  
  // wypisuję tutaj kolejne rzeczy do konsoli, nie jest to wazne do działania
  Serial.print("kat s1 (0-1): ");
  Serial.print(kat_y);
  Serial.print(" real s1 (0-180): ");
  Serial.println(pozycja_s1);
  Serial.print("kat s2 (0-1): ");
  Serial.print(kat_x);
  Serial.print(" real s2 (0-180): ");
  Serial.println(pozycja_s2);  
  
  // Do Debugu:
  // Serial.print("Suma: ");
  // Serial.println(suma);
  // Serial.print("Różnica_y: ");
  // Serial.println(roznica_y);
  // Serial.print("Różnica_x: ");
  // Serial.println(roznica_x);    
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//TRYB CALC
void calc(){
      time_t utc = now();
    calcHorizontalCoordinates(utc, latitude, longitude, azimuth, elevation);
    printUTC(utc);
    Serial.print("Azymut: ");
    Serial.println(azimuth);
    Serial.print("Wysokosc: ");
    Serial.println(elevation);
    Serial.println();

    // okazuje się, że cały czas liczyłem kąt źle :(, to jest stara wersja.
    // if(elevation>0){
    //   if(azimuth<180){
    //     target_s1 = elevation/2;
    //     target_s2 = azimuth/2+90+kierunek;
    //   }
    //   if(azimuth>180){
    //     target_s1 = elevation/2;
    //     target_s2 = -90+azimuth/2+90+kierunek;      
    //   }
    // }

  if(elevation > 0)
  {
      double As = radians(azimuth);
      double hs = radians(elevation);

      // Wektor do Słońca
      double Sx = cos(hs) * sin(As);
      double Sy = cos(hs) * cos(As);
      double Sz = sin(hs);

      // Kierunek odbicia:
      // północ poziomo
      double Tx = 0.0;
      double Ty = -1.0;
      double Tz = 0.0;

      // Dwusieczna = normalna lustra
      double Nx = Sx + Tx;
      double Ny = Sy + Ty;
      double Nz = Sz + Tz;

      // Normalizacja
      double norm = sqrt(Nx*Nx + Ny*Ny + Nz*Nz);

      Nx /= norm;
      Ny /= norm;
      Nz /= norm;

      // Kąty normalnej lustra
      double mirrorAzimuth   = degrees(atan2(Nx, Ny));
      double mirrorElevation = degrees(asin(Nz));

      double servoAzimuth = mirrorAzimuth+90;

      // if(servoAzimuth < 180) servoAzimuth += 180;
      if(servoAzimuth >= 180) servoAzimuth -= 180;

      Serial.print("Mirror azimuth: ");
      Serial.println(servoAzimuth);

      Serial.print("Mirror elevation: ");
      Serial.println(mirrorElevation);

      target_s1 = mirrorElevation+poprawka_krzywy;
      target_s2 = servoAzimuth;
  }    

    else{
      Serial.println("Jest noc!");
    }  
    delay(2000); //czeka dodatkowe 2s (w sumie 3s default)    
  
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Tutaj funkcja, która liczy średnią z 5 kolejnych pomiarów z dzielnika napięcia.
// Powinno to sprawić, że troszeczkę mniej będzie skakał.
// Potem powininienem zaimplementować średnią ruchomą co pewnie da jeszcze lepszy efekt, ale mniejsza na razie. 
int averageAnalogRead(int pin)
{
    long suma = 0;

    for (int i = 0; i < srednie; i++)
    {
        suma += analogRead(pin);
    }

    return suma / srednie;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//10.06.2026 taka funkcje dodalem by ruszala servo powoli:
void moveServoSlow(Servo &servo, int target, int &pozycja)
{
    while (pozycja != target)
    {
        if (pozycja < target)
            pozycja++;
        else
            pozycja--;

        servo.write(pozycja);
        delay(moveInterval);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//do obslugi przycisku, by nacisniecie przycisku zawsze działało (przerywalo inne procesy)
void buttonISR()
{
    unsigned long now = millis();

    if (now - lastInterrupt > 200) {
        buttonPressed = true;
        lastInterrupt = now;
    }
}

