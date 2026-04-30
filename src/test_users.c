#include <stdio.h>                                                                                                                                        
#include <stdlib.h>                                                                                                                                       
#include "users.h"                                                                                                                                   
                                                                                                                                                            
  int main(void) {                                                                                                                                          
      // REGISTER                                                                                                                                           
      printf("add alice:    %d (esperado 0)\n", user_add("alice"));                                                                                         
      printf("add alice:    %d (esperado 1)\n", user_add("alice")); // ya existe                                                                            
      printf("add bob:      %d (esperado 0)\n", user_add("bob"));                                                                                           
                                                                                                                                                            
      // FIND                                                                                                                                               
      printf("find alice:   %d (esperado 0)\n", user_find("alice"));
      printf("find nobody:  %d (esperado -1)\n", user_find("nobody"));                                                                                      
                                                                                                                                                            
      // CONNECT                                                                                                                                            
      printf("connect alice: %d (esperado 0)\n", user_connect("alice", "127.0.0.1", 9000));                                                                 
      printf("connect alice: %d (esperado 2)\n", user_connect("alice", "127.0.0.1", 9000)); // ya conectado                                                 
      printf("connect nobody:%d (esperado 1)\n", user_connect("nobody", "127.0.0.1", 9001));                                                                
                                                                                                                                                            
      // MENSAJES                                                                                                                                           
      int idx = user_find("bob");                                                                                                                           
      unsigned int id1 = msg_add(idx, "alice", "hola bob");
      unsigned int id2 = msg_add(idx, "alice", "segundo mensaje");                                                                                          
      printf("msg id1: %u (esperado 1)\n", id1);
      printf("msg id2: %u (esperado 2)\n", id2);                                                                                                            
                  
      Msg *m = msg_pop(idx);                                                                                                                                
      printf("msg pop: '%s' id=%u\n", m->text, m->id);
      free(m);                                                                                                                                              
                  
      // DISCONNECT                                                                                                                                         
      printf("disconnect alice: %d (esperado 0)\n", user_disconnect("alice"));
      printf("disconnect alice: %d (esperado 2)\n", user_disconnect("alice")); // no conectado
                                                                                                                                                            
      // UNREGISTER
      printf("remove bob:   %d (esperado 0)\n", user_remove("bob"));                                                                                        
      printf("remove bob:   %d (esperado 1)\n", user_remove("bob")); // no existe                                                                           
                                                                                                                                                            
      return 0;                                                                                                                                             
  }