# Спецификация языка программирования "ALK-V"

## 1. Обзор языка

ALK-V - статически типизированный язык программирования с автоматическим управлением памятью, разработанный для исполнения на виртуальной машине с JIT-компиляцией.

**Ключевые особенности:**
- Статическая типизация с выводом типов в некоторых случаях
- Объектно-ориентированная модель с классами
- Автоматическое управление памятью (GC)
- Поддержка функциональных элементов (рекурсия)
- Безопасность памяти и контроль доступа

## 2. Синтаксис и основные конструкции

### 2.1 Объявление переменных
```ALK-V
переменная : тип = значение;
// Примеры:
x : int = 10;
имя : string = "str";
массив : int[3] = [1, 2, 3];
```

### 2.2 Функции
```ALK-V
fnc имя_функции : возвращаемый_тип (параметры) {
    // тело функции
}

// Пример:
fnc factorial : int (n : int) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}
```

### 2.3 Классы и методы
```ALK-V
class ИмяКласса {
public:
    поле : тип;
    
    method : тип_возврата (параметры) {
        // реализация
    }
    
    Конструктор(параметры) {
        // инициализация
    }

private:
    приватное_поле : тип;
}

// Пример:
class Shtuka {
public:
    hihihoho : int;
    
    method : int(n : int) {
        // реализация метода
    }

    Shtuka(args) {
        // конструктор
    }

private:
    secret : bool;    
}
```

### 2.4 Управляющие конструкции

**Условия:**
```ALK-V
if (условие) {
    // код
} else if (другое_условие) {
    // код
} else {
    // код
}
```

**Циклы:**
```ALK-V
// While цикл
while (условие) {
    // код
}

// For цикл с диапазоном
for (i in начало...конец) {
    // код
}

// For цикл с условием
for (i : int = 0; i < n; i = i + 1) {
    // код
}
```

**Switch:**
```ALK-V
switch (выражение) {
    значение1 => действие1;
    значение2 => действие2;
    _ => действие_по_умолчанию;
}
```

## 3. Система типов

### 3.1 Базовые типы
- `int` - целые числа
- `float` - числа с плавающей точкой  
- `bool` - логический тип (`T`/`F`)
- `string` - строки
- `type[n]` - массивы
- `void` - отсутствие значения

### 3.2 Составные типы
- **Классы** - пользовательские типы данных
- **Массивы** - гомогенные коллекции
- **Maybe-типы** (планируется) для обработки отсутствующих значений

## 4. Стандартная библиотека "ochev"

Библиотека для базовых операций ввода-вывода и утилит:

```ALK-V
ochev.TudaSyuda(arr[i], arr[j]) // обмен элементов
ochev.Out(значение)             // вывод
ochev.In(переменная)            // ввод
ochev.>>>(a, b)                 // max
ochev.<<<(a, b)                 // min
```

## 5. Операторы

### 5.1 Арифметические
- `+`, `-`, `*`, `/`, `%`

### 5.2 Сравнения  
- `!=`, `=?`, `<`, `>`, `<=`, `>=`

### 5.3 Логические
- `&&` (И), `||` (ИЛИ), `!` (НЕ)

## 6. Архитектура исполнения

### 6.1 Виртуальная машина ALK-VM
- **Байткод** 
- **Стек исполнения** 

### 6.2 Менеджер памяти
- **Автоматический GC** (Serial GC)
- **Сборка мусора** с паузами (stop the world)
- **Оптимизация аллокаций** для массивов

### 6.3 JIT-компилятор
- **Теплые пути** - компиляция при повторных вызовах
- **Инлайнинг** маленьких функций
- **Профилирование** для часто используемых участков

## 7. Примеры реализации benchmark'ов

### 7.1 Факториал (рекурсивный)
```ALK-V
fnc factorial : int (n : int) {
    if (n == 0) {
        return 1;
    }
    return n * factorial(n - 1);
}

fnc main : int () {
    start : int = ochev.Epoch.Now();
    ochev.Out("Factorial 20: " + factorial(20));
    end : int = ochev.Epoch.Now();
    ochev.Out("Time: " + (end - start) + "ms");
}
```

### 7.2 Сортировка пузырьком
```ALK-V
fnc bubbleSort : void (arr : int[], size : int) {
    sorted : bool = F;
    while (!sorted) {
        sorted = T;
        for (i in 1...size-1) {
            if (arr[i] < arr[i-1]) {
                sorted = F;
                ochev.tudaSyuda(arr[i], arr[i-1]);
            }
        }
    }
}

fnc main : int {
    start : int = ochev.Epoch.Now();
    n : int = 10000;
    baseArray : int[n];
    for (i in 0...n) {
        baseArray[i] = ochev.Chaos.Integer();
    }
    bubbleSort(baseArray, n);
    end : int = ochev.Epoch.Now();
    ochev.Out("Time: " + (end - start) + "ms");
}
```

### 7.3 Решето Эратосфена
```ALK-V
fnc sieve : bool[] (n : int) {
    primes : bool[n];
    
    for (i in 0...n-1) {
        primes[i] = T;
    }
    
    for (i in 2...n-1) {
        if (primes[i]) {
            j : int = i * i;
            while (j < n) {
                primes[j] = F;
                j = j + i;
            }
        }
    }
    return primes;
}

fnc main : int {
    result : bool[] = sieve(100000);
    for (i in 0...100000) {
        ochev.Out(i);
    }
}
```

### 7.4 Квиксорт
```
fnc partition : int (arr : int[], low : int, high : int) {
    pivot : int = arr[high];
    i : int = low - 1;
    j : int = low;

    while (j < high) {
        if (arr[j] < pivot) {
            i = i + 1;
            ochev.TudaSyuda(arr[i], arr[j]);
        }
        j = j + 1;
    }

    ochev.TudaSyuda(arr[i + 1], arr[high]);
    return i + 1;
}

fnc quickSort : void (arr : int[], low : int, high : int) {
    if (low < high) {
        p : int = partition(arr, low, high);
        quickSort(arr, low, p - 1);
        quickSort(arr, p + 1, high);
    }
}

fnc main : int () {
    n : int = 10000;
    arr : int[n];

    for (i in 0...n) {
        arr[i] = ochev.Chaos.Integer();
    }

    start : int = ochev.Epoch.Now();
    quickSort(arr, 0, n - 1);
    end : int = ochev.Epoch.Now();

    ochev.Out("Time: " + (end - start) + "ms");
}
```

## 8. Архитектура
[ ALK-V source ]\
│\
▼\
[ Java compiler ]\
├─ Lexer\
├─ Parser\
├─ AST\
├─ Type checker\
└─ Bytecode (.alkb)\
│\
▼\
[ Native runtime ]\
├─ Bytecode loader\
├─ Interpreter\
├─ Custom heap\
├─ Custom GC\
└─ Handwritten JIT → machine code


## 9. План разработки

### Этап 1: Ядро языка
- [x] Лексический анализатор
- [x] Парсер AST
- [x] Базовая система типов

### Этап 2: Виртуальная машина  
- [x] Байткод спецификация
- [x] Интерпретатор
- [x] Система управления памятью

### Этап 3: Оптимизации
- [x] JIT-компиляция
- [x] Оптимизации GC
- [ ] Стандартная библиотека







