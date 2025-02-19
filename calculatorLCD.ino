#include <avr/pgmspace.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Protótipos para funções usadas antes da definição
void formatResultFloat(float res, char *resultStr, int size);

// Constantes e definições básicas
#define SQRT_SYMBOL "S"                   // Símbolo para raiz quadrada
#define PI_SYMBOL "π"                     // Símbolo para pi
#define ERROR_MSG "ERROR"                 // Mensagem de erro exibida
#define ERROR_MSG_LEN 5
#define NOSOL_MSG "Ø"                     // Sem solução
#define INF_SOL_MSG "Inf. sol."           // Soluções infinitas

#define MAX_POLY_DEGREE 5                 // Suporte até grau 5
#define TOLERANCE 1e-6

// Declarações globais necessárias para o buffer e estado da interface
char eqInputBuffer[17] = "";
uint8_t eqInputLength = 0;
uint8_t cursorPosition = 0;
bool messageDisplayed = false;

// Definições dos pinos do LCD
#define LCD_RS_PIN  A1
#define LCD_RW_PIN  A0
#define LCD_E_PIN   2
#define LCD_D4_PIN  A2
#define LCD_D5_PIN  A3
#define LCD_D6_PIN  A4
#define LCD_D7_PIN  A5
#define LCD_RS_MASK (1<<PC1)
#define LCD_RW_MASK (1<<PC0)
#define LCD_D4_MASK (1<<PC2)
#define LCD_D5_MASK (1<<PC3)
#define LCD_D6_MASK (1<<PC4)
#define LCD_D7_MASK (1<<PC5)
#define LCD_E_MASK  (1<<PD2)

// Funções básicas do LCD
static inline void lcdSendNibble(uint8_t nibble) { 
  PORTC = (PORTC & ~((1 << PC2)|(1 << PC3)|(1 << PC4)|(1 << PC5))) | ((nibble & 0x0F) << 2);
  PORTD |= LCD_E_MASK; delayMicroseconds(1); PORTD &= ~LCD_E_MASK; delayMicroseconds(80);
}
static inline void lcdCommand(uint8_t cmd) { 
  PORTC &= ~((1 << PC1)|(1 << PC0)); 
  lcdSendNibble(cmd >> 4); 
  lcdSendNibble(cmd & 0x0F); 
  delay(2);
}
static inline void lcdWriteData(uint8_t data) { 
  PORTC = (PORTC & ~((1 << PC1)|(1 << PC0))) | (1 << PC1); 
  lcdSendNibble(data >> 4); 
  lcdSendNibble(data & 0x0F); 
  delay(2);
}
void lcdInit() { 
  DDRC |= (1 << PC0)|(1 << PC1)|(1 << PC2)|(1 << PC3)|(1 << PC4)|(1 << PC5); 
  DDRD |= (1 << PD2);
  PORTC &= ~((1 << PC0)|(1 << PC1)|(1 << PC2)|(1 << PC3)|(1 << PC4)|(1 << PC5)); 
  PORTD &= ~LCD_E_MASK; 
  delay(50);
  lcdCommand(0x01); delay(2); 
  lcdSendNibble(0x03); delay(5); 
  lcdSendNibble(0x03); delay(5);
  lcdSendNibble(0x03); delayMicroseconds(150); 
  lcdSendNibble(0x02); 
  lcdCommand(0x28); 
  lcdCommand(0x08);
  lcdCommand(0x01); delay(2); 
  lcdCommand(0x06); 
  lcdCommand(0x0C);
}
void lcdClear() { lcdCommand(0x01); delay(2); }
void lcdSetCursor(uint8_t col, uint8_t row) { 
  const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54}; 
  lcdCommand(0x80 | (col + row_offsets[row])); 
}
void lcdPrint(const char *str) { 
  while (*str) 
    lcdWriteData(*str++); 
}

// Configuração do teclado matricial
const byte rowPins[4] = {7, 8, 9, 10};
const byte colPins[4] = {3, 4, 5, 6};
void keypadInit() { 
  for (int i = 0; i < 4; i++) { 
    pinMode(rowPins[i], OUTPUT); 
    digitalWrite(rowPins[i], HIGH); 
  } 
  for (int i = 0; i < 4; i++) { 
    pinMode(colPins[i], INPUT_PULLUP); 
  } 
}
int getKeyIndex() { 
  for (int r = 0; r < 4; r++) { 
    digitalWrite(rowPins[r], LOW); 
    uint8_t portD = PIND; 
    for (int c = 0; c < 4; c++) { 
      if (!(portD & (1 << colPins[c]))) { 
        delay(50); 
        while (!(PIND & (1 << colPins[c]))); 
        digitalWrite(rowPins[r], HIGH); 
        return r * 4 + c; 
      } 
    } 
    digitalWrite(rowPins[r], HIGH); 
  } 
  return -1; 
}

// Conversão de string para float e validação de entrada
float my_strtof(const char *str, char **endptr) { 
  float result = 0.0; 
  bool negative = false; 
  const char *ptr = str; 
  while (isspace(*ptr)) ptr++; 
  if (*ptr == '-') { negative = true; ptr++; } 
  else if (*ptr == '+') { ptr++; }
  while (isdigit(*ptr)) { result = result * 10 + (*ptr - '0'); ptr++; }
  if (*ptr == '.') { 
    ptr++; 
    float fraction = 0.0, divisor = 10.0; 
    while (isdigit(*ptr)) { 
      fraction += (*ptr - '0') / divisor; 
      divisor *= 10.0; 
      ptr++; 
    } 
    result += fraction; 
  }
  if (endptr) *endptr = (char*)ptr; 
  return negative ? -result : result; 
}
static inline bool validateInput(const char* input) { 
  // Acrescentado o caractere π na lista de permitidos
  const char* allowed = "0123456789 +-*/^.()SπxyzXYZ="; 
  for (const char *p = input; *p; p++) { 
    if (!isspace(*p) && !strchr(allowed, *p)) 
      return false; 
  }
  return true; 
}

// Parser recursivo para expressões matemáticas
const char *expr_ptr;
void skipWhitespace() { 
  while (*expr_ptr == ' ') 
    expr_ptr++; 
}
float parseExpression();
float parsePrimary() { 
  skipWhitespace(); 
  if (*expr_ptr == '(') { 
    expr_ptr++; 
    float result = parseExpression(); 
    if (*expr_ptr == ')') 
      expr_ptr++; 
    return result; 
  }
  // Verifica se a expressão começa com π, usando a constante PI_SYMBOL
  if (strncmp(expr_ptr, PI_SYMBOL, strlen(PI_SYMBOL)) == 0) {
    expr_ptr += strlen(PI_SYMBOL);
    return 3.14159265; // Valor aproximado de π
  }
  if (strncmp(expr_ptr, SQRT_SYMBOL, strlen(SQRT_SYMBOL)) == 0) { 
    expr_ptr += strlen(SQRT_SYMBOL); 
    return sqrt(parsePrimary()); 
  }
  char *end; 
  float result = my_strtof(expr_ptr, &end); 
  expr_ptr = end; 
  return result; 
}
float parseFactor() { 
  float base = parsePrimary(); 
  skipWhitespace(); 
  if (*expr_ptr == '^') { 
    expr_ptr++; 
    float exponent = parseFactor(); 
    return pow(base, exponent); 
  } 
  return base; 
}
float parseTerm() { 
  float result = parseFactor(); 
  skipWhitespace(); 
  while (*expr_ptr == '*' || *expr_ptr == '/') { 
    char op = *expr_ptr++; 
    float factor = parseFactor(); 
    result = (op == '*') ? result * factor : result / factor; 
    skipWhitespace(); 
  } 
  return result; 
}
float parseExpression() { 
  float result = parseTerm(); 
  skipWhitespace(); 
  while (*expr_ptr == '+' || *expr_ptr == '-') { 
    char op = *expr_ptr++; 
    float term = parseTerm(); 
    result = (op == '+') ? result + term : result - term; 
    skipWhitespace(); 
  } 
  return result; 
}
float evaluateExpression(const char *expr) { 
  if (!validateInput(expr)) 
    return NAN; 
  expr_ptr = expr; 
  return parseExpression(); 
}

// Função para avaliar um polinômio de grau n em x usando o método de Horner
float evaluatePoly(const float poly[], int degree, float x) {
  float result = poly[degree];
  for (int i = degree - 1; i >= 0; i--) {
    result = result * x + poly[i];
  }
  return result;
}

// Função para encontrar raízes reais de um polinômio usando amostragem e bissecção
int findRealPolynomialRoots(const float poly[], int degree, float rootsFound[]) {
  float tol = TOLERANCE;
  // Estima um limite para as raízes: |x| <= 1 + max(|poly[i]|/|poly[degree]|)
  float maxCoeff = 0;
  for (int i = 0; i < degree; i++) {
    float val = fabs(poly[i]);
    if (val > maxCoeff) 
      maxCoeff = val;
  }
  float bound = 1 + maxCoeff / fabs(poly[degree]);
  
  int count = 0;
  const int samples = 200;
  float step = (2 * bound) / samples;
  float prev_x = -bound;
  float prev_val = evaluatePoly(poly, degree, prev_x);
  
  for (int i = 1; i <= samples; i++) {
    float x = -bound + i * step;
    float val = evaluatePoly(poly, degree, x);
    
    // Se o valor estiver próximo de zero, considere como raiz
    if (fabs(val) < tol) {
      bool duplicate = false;
      for (int j = 0; j < count; j++) {
        if (fabs(rootsFound[j] - x) < tol) { 
          duplicate = true; 
          break; 
        }
      }
      if (!duplicate)
        rootsFound[count++] = x;
    }
    // Se ocorrer mudança de sinal, aplique bissecção
    if (prev_val * val < 0) {
      float a = prev_x, b = x;
      float fa = prev_val, fb = val;
      for (int iter = 0; iter < 20; iter++) {
        float mid = (a + b) / 2;
        float fmid = evaluatePoly(poly, degree, mid);
        if (fabs(fmid) < tol) {
          a = mid;
          b = mid;
          break;
        }
        if (fa * fmid < 0) {
          b = mid;
          fb = fmid;
        } else {
          a = mid;
          fa = fmid;
        }
      }
      float root = (a + b) / 2;
      bool duplicate = false;
      for (int j = 0; j < count; j++) {
        if (fabs(rootsFound[j] - root) < tol) { 
          duplicate = true; 
          break; 
        }
      }
      if (!duplicate)
        rootsFound[count++] = root;
    }
    prev_x = x;
    prev_val = val;
  }
  return count;
}

// Função para resolver equações polinomiais com variável (x, y ou z)
// Retorna:
//   -1 : erro de parsing
//   -2 : soluções infinitas
//    0 : sem solução real
//    1 : solução única
//    2 : duas (ou mais) soluções reais – as duas primeiras serão exibidas
int solvePolynomialEquation(const char *eq, float roots[], char var) {
  if (!validateInput(eq)) 
    return -1;
  char eqCopy[33]; 
  strncpy(eqCopy, eq, 32); 
  eqCopy[32] = '\0';
  char *equalSign = strchr(eqCopy, '=');
  if (!equalSign) 
    return -1;
  *equalSign = '\0';
  char *lhs = eqCopy, *rhs = equalSign + 1;
  
  float polyL[MAX_POLY_DEGREE+1] = {0};
  float polyR[MAX_POLY_DEGREE+1] = {0};
  
  // Função inline para processar um lado da equação
  auto processSide = [var](const char *side, float polyArr[MAX_POLY_DEGREE+1]) {
    const char *p = side;
    while (*p) {
      while (*p == ' ') 
        p++;
      if (!*p) break;
      int sign = 1;
      if (*p == '+' || *p == '-') {
        if (*p == '-') 
          sign = -1;
        p++;
      }
      char *end;
      float coeff = my_strtof(p, &end);
      if (end != p) {
        p = end;
      } else {
        coeff = 1;
      }
      coeff *= sign;
      int exponent = 0;
      if (*p == var || *p == toupper(var)) {
        p++;
        exponent = 1;
        if (*p == '^') {
          p++;
          exponent = (int)my_strtof(p, &end);
          p = end;
        }
      } else {
        exponent = 0;
      }
      if (exponent < 0 || exponent > MAX_POLY_DEGREE) {
        // Exponente fora do suporte: sinaliza erro
        polyArr[0] = NAN;
        return;
      }
      polyArr[exponent] += coeff;
    }
  };
  
  processSide(lhs, polyL);
  processSide(rhs, polyR);
  
  if (isnan(polyL[0]) || isnan(polyR[0])) 
    return -1;
  
  float poly[MAX_POLY_DEGREE+1];
  for (int i = 0; i <= MAX_POLY_DEGREE; i++) {
    poly[i] = polyL[i] - polyR[i];
  }
  
  // Determina o grau real do polinômio
  int degree = MAX_POLY_DEGREE;
  while (degree > 0 && fabs(poly[degree]) < TOLERANCE) {
    degree--;
  }
  
  if (degree == 0) {
    if (fabs(poly[0]) < TOLERANCE)
      return -2; // Soluções infinitas
    else
      return 0;  // Sem solução
  }
  
  if (degree == 1) {
    roots[0] = -poly[0] / poly[1];
    return 1;
  }
  
  if (degree == 2) {
    float a = poly[2], b = poly[1], c = poly[0];
    float disc = b * b - 4 * a * c;
    if (disc < 0) 
      return 0;
    else if (fabs(disc) < TOLERANCE) {
      roots[0] = -b / (2 * a);
      return 1;
    } else {
      float sol1 = (-b + sqrt(disc)) / (2 * a);
      float sol2 = (-b - sqrt(disc)) / (2 * a);
      roots[0] = sol1;
      roots[1] = sol2;
      return 2;
    }
  }
  
  // Para grau ≥ 3, usa o método de amostragem e bissecção
  float foundRoots[MAX_POLY_DEGREE] = {0};
  int count = findRealPolynomialRoots(poly, degree, foundRoots);
  if (count == 0) 
    return 0;
  else if (count == 1) {
    roots[0] = foundRoots[0];
    return 1;
  } else {
    roots[0] = foundRoots[0];
    roots[1] = foundRoots[1];
    return 2;
  }
}

// Função auxiliar para contar variáveis na entrada
int countVariables(const char *input) {
  bool hasX = false, hasY = false, hasZ = false;
  for (const char *p = input; *p; p++) {
    char c = *p;
    if (c == 'x' || c == 'X') hasX = true;
    if (c == 'y' || c == 'Y') hasY = true;
    if (c == 'z' || c == 'Z') hasZ = true;
  }
  int count = 0;
  if (hasX) count++;
  if (hasY) count++;
  if (hasZ) count++;
  return count;
}

// Função para resolver equações lineares simples (expressões sem variáveis)
float solveLinearEquation(const char *eq, bool *valid) {
  char eqCopy[33]; 
  strncpy(eqCopy, eq, 32); 
  eqCopy[32] = '\0';
  char *equalSign = strchr(eqCopy, '=');
  if (!equalSign) { 
    *valid = false; 
    return 0.0; 
  }
  *equalSign = '\0';
  // Expressões sem variável são calculadas como expressões matemáticas
  *valid = true; 
  return evaluateExpression(eq);
}

// Função para converter resultado em string para exibição com notação científica se necessário
void formatResultFloat(float res, char *resultStr, int size) {
  char buffer[17];
  if ((fabs(res) >= 100000) || (fabs(res) > 0 && fabs(res) < 0.0001)) {
    snprintf(buffer, sizeof(buffer), "%.2e", res);
  } else {
    dtostrf(res, 0, 2, buffer);
    char *dot = strchr(buffer, '.');
    if (dot != NULL && strcmp(dot, ".00") == 0) 
      *dot = '\0';
  }
  if (strlen(buffer) >= (unsigned)size) {
    snprintf(buffer, sizeof(buffer), "%.2e", res);
    if (strlen(buffer) >= (unsigned)size) {
      buffer[size-1] = '\0';
    }
  }
  strncpy(resultStr, buffer, size); 
  resultStr[size - 1] = '\0';
}

// Funções auxiliares para atualizar o buffer e exibir mensagens não editáveis
void updateBufferWithResult(const char *result) {
  strncpy(eqInputBuffer, result, 17);
  eqInputBuffer[16] = '\0';
  eqInputLength = strlen(eqInputBuffer);
  cursorPosition = eqInputLength;
}

void displayMessage(const char *msg) {
  updateBufferWithResult(msg);
  messageDisplayed = true;
}

// Entrada e interface do teclado multitap
#define MULTITAP_TIMEOUT 2500
#define BLINK_INTERVAL 500
int currentKey_input = -1;
int currentTokenIndex_input = 0;
unsigned long lastKeyPressTime_input = 0;
bool multiTapActive_input = false;
unsigned long lastBlinkTime_input = 0;
bool blinkState_input = false;
const char* key0Options_input[] = {"1"};
const char* key1Options_input[] = {"2"};
const char* key2Options_input[] = {"3"};
const char* key3Options_input[] = {"4"};
const char* key4Options_input[] = {"5"};
const char* key5Options_input[] = {"6"};
const char* key6Options_input[] = {"7"};
const char* key7Options_input[] = {"8"};
const char* key8Options_input[] = {"9"};
const char* key9Options_input[] = {"0"};
const char* key10Options_input[] = {"BK"};
const char* key11Options_input[] = {"ENT"};
// Adicionado o símbolo π junto com as variáveis x, y e z
const char* key12Options_input[] = {"x", "y", "z", "π"};
const char* key13Options_input[] = {"+", "-", "*", "/", "(", ")", ".", "=", "S", "^"};
const char* key14Options_input[] = {"LFT"};
const char* key15Options_input[] = {"RGT"};
struct KeyMapping_input { const char **options; uint8_t numOptions; };
KeyMapping_input eqKeyMap_input[16] = {
  { key0Options_input, 1 }, { key1Options_input, 1 }, { key2Options_input, 1 }, { key3Options_input, 1 },
  { key4Options_input, 1 }, { key5Options_input, 1 }, { key6Options_input, 1 }, { key7Options_input, 1 },
  { key8Options_input, 1 }, { key9Options_input, 1 }, { key10Options_input, 1 }, { key11Options_input, 1 },
  { key12Options_input, 4 },  // Atualizado para 4 opções: x, y, z e π
  { key13Options_input, 10 }, { key14Options_input, 1 }, { key15Options_input, 1 }
};
static inline bool isMultiTapKey(int keyIndex) { 
  return (keyIndex == 12 || keyIndex == 13); 
}
  
// Processamento do teclado
void insertTokenAtCursor_input(const char *token) {
  uint8_t tokenLen = strlen(token);
  if (eqInputLength + tokenLen <= 16) {
    for (int i = eqInputLength; i >= cursorPosition; i--) { 
      eqInputBuffer[i + tokenLen] = eqInputBuffer[i]; 
    }
    for (int i = 0; i < tokenLen; i++) { 
      eqInputBuffer[cursorPosition + i] = token[i]; 
    }
    eqInputLength += tokenLen; 
    cursorPosition += tokenLen;
  }
}
void removeTokenAtCursor_input() {
  if (cursorPosition > 0) { 
    for (int i = cursorPosition - 1; i < eqInputLength; i++) { 
      eqInputBuffer[i] = eqInputBuffer[i + 1]; 
    } 
    eqInputLength--; 
    cursorPosition--; 
  }
}
void moveCursorLeft_input() { 
  if (cursorPosition > 0) 
    cursorPosition--; 
}
void moveCursorRight_input() { 
  if (cursorPosition < eqInputLength) 
    cursorPosition++; 
}
void updateEquationDisplay_input() {
  if (millis() - lastBlinkTime_input >= BLINK_INTERVAL) { 
    blinkState_input = !blinkState_input; 
    lastBlinkTime_input = millis(); 
  }
  char displayBuffer[17]; 
  memset(displayBuffer, ' ', 16); 
  displayBuffer[16] = '\0'; 
  memcpy(displayBuffer, eqInputBuffer, eqInputLength);
  if (multiTapActive_input) { 
    const char *token = eqKeyMap_input[currentKey_input].options[currentTokenIndex_input]; 
    uint8_t tokenLen = strlen(token); 
    if (cursorPosition + tokenLen <= 16) 
      memcpy(displayBuffer + cursorPosition, token, tokenLen); 
  }
  else { 
    char original = (cursorPosition < eqInputLength) ? eqInputBuffer[cursorPosition] : ' '; 
    displayBuffer[cursorPosition] = blinkState_input ? '_' : original; 
  }
  lcdSetCursor(0, 1); 
  lcdPrint(displayBuffer);
}
void processKeyPress_input(int keyIndex) {
  // Se uma mensagem não editável estiver sendo exibida, somente o ENTER (keyIndex==11) a descarta
  if (messageDisplayed) {
    if (keyIndex == 11) {
      eqInputBuffer[0] = '\0';
      eqInputLength = 0;
      cursorPosition = 0;
      messageDisplayed = false;
    }
    return;
  }
  unsigned long now = millis();
  if (keyIndex == 10 || keyIndex == 11 || keyIndex == 14 || keyIndex == 15) {
    if (multiTapActive_input) { 
      insertTokenAtCursor_input(eqKeyMap_input[currentKey_input].options[currentTokenIndex_input]); 
      multiTapActive_input = false; 
    }
    if (keyIndex == 10) { 
      removeTokenAtCursor_input(); 
    }
    else if (keyIndex == 11) {
      char resultStr[17];
      // Se a entrada contiver variável, processa como equação polinomial
      if (strchr(eqInputBuffer, 'x') || strchr(eqInputBuffer, 'X') ||
          strchr(eqInputBuffer, 'y') || strchr(eqInputBuffer, 'Y') ||
          strchr(eqInputBuffer, 'z') || strchr(eqInputBuffer, 'Z')) {
        char var;
        if (strchr(eqInputBuffer, 'x') || strchr(eqInputBuffer, 'X'))
          var = 'x';
        else if (strchr(eqInputBuffer, 'y') || strchr(eqInputBuffer, 'Y'))
          var = 'y';
        else if (strchr(eqInputBuffer, 'z') || strchr(eqInputBuffer, 'Z'))
          var = 'z';
        else {
          displayMessage(ERROR_MSG);
          return;
        }
        float roots[2];
        int numRoots = solvePolynomialEquation(eqInputBuffer, roots, var);
        if (numRoots == -1) {
          displayMessage(ERROR_MSG);
        } else if (numRoots == -2) { // Infinitas soluções
          displayMessage(INF_SOL_MSG);
        } else if (numRoots == 0) {  // Sem solução
          displayMessage(NOSOL_MSG);
        } else if (numRoots == 1) {
          formatResultFloat(roots[0], resultStr, 17);
          updateBufferWithResult(resultStr);
        } else if (numRoots >= 2) {
          char temp1[9], temp2[9];
          formatResultFloat(roots[0], temp1, 9);
          formatResultFloat(roots[1], temp2, 9);
          snprintf(resultStr, 17, "%c=%s %c=%s", var, temp1, var, temp2);
          displayMessage(resultStr);
        }
      }
      // Caso contrário, trata a entrada como expressão matemática geral
      else {
        float res = evaluateExpression(eqInputBuffer);
        if (isnan(res)) { 
          displayMessage(ERROR_MSG);
        }
        else if (isinf(res)) { 
          displayMessage(INF_SOL_MSG); 
        }
        else { 
          formatResultFloat(res, resultStr, 17); 
          updateBufferWithResult(resultStr);
        }
      }
    }
    else if (keyIndex == 14) { 
      moveCursorLeft_input(); 
    }
    else if (keyIndex == 15) { 
      moveCursorRight_input(); 
    }
    return;
  }
  if (!isMultiTapKey(keyIndex)) { 
    insertTokenAtCursor_input(eqKeyMap_input[keyIndex].options[0]); 
    lastKeyPressTime_input = now; 
    return; 
  }
  if (multiTapActive_input && keyIndex == currentKey_input && (now - lastKeyPressTime_input < MULTITAP_TIMEOUT))
    currentTokenIndex_input = (currentTokenIndex_input + 1) % eqKeyMap_input[keyIndex].numOptions;
  else { 
    if (multiTapActive_input) 
      insertTokenAtCursor_input(eqKeyMap_input[currentKey_input].options[currentTokenIndex_input]);
    currentKey_input = keyIndex; 
    currentTokenIndex_input = 0; 
    multiTapActive_input = isMultiTapKey(keyIndex);
  }
  lastKeyPressTime_input = now;
}
void checkMultiTapTimeout_input() {
  if (multiTapActive_input && isMultiTapKey(currentKey_input) && (millis() - lastKeyPressTime_input >= MULTITAP_TIMEOUT)) {
    insertTokenAtCursor_input(eqKeyMap_input[currentKey_input].options[currentTokenIndex_input]);
    multiTapActive_input = false;
  }
}

// Setup e loop principal
void setup() {
  lcdInit(); 
  keypadInit(); 
  lcdClear(); 
  lcdSetCursor(0, 0); 
  lcdPrint("SciCalc-Equation");
  eqInputBuffer[0] = '\0'; 
  eqInputLength = 0; 
  cursorPosition = 0; 
  lastBlinkTime_input = millis(); 
  blinkState_input = false;
}
void loop() {
  int keyIndex = getKeyIndex(); 
  if (keyIndex != -1) 
    processKeyPress_input(keyIndex);
  checkMultiTapTimeout_input(); 
  updateEquationDisplay_input();
}
