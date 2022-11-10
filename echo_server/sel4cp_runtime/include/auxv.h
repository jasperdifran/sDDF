typedef struct
{
    int a_type;
    union
    {
        long a_val;
        void *a_ptr;
        void (*a_fnc)(void);
    } a_un;
} auxv_t;
