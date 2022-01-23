concrete BindCnc of Bind = open Prelude in {
  lincat
    S = SS ;
  lin
    f1 = ss ("hello the" ++ BIND ++ "re") ;
    f2 = ss ("good" ++ SOFT_BIND ++ "bye") ;
    concat a b = ss (a.s ++ b.s) ;
    bind a b = ss (a.s ++ BIND ++ b.s) ;
    softbind a b = ss (a.s ++ SOFT_BIND ++ b.s) ;
    softspace a b = ss (a.s ++ SOFT_SPACE ++ b.s) ;
    capit a = ss (CAPIT ++ a.s) ;
    allcapit a = ss (ALL_CAPIT ++ a.s) ;
    prebind a = ss (p ++ a.s) ;
    precapit a = ss (p ++ CAPIT ++ a.s) ;
  oper
    p = pre {
      "he" => "|" ++ BIND;
      "H"|"G" => "^" ++ BIND;
      _ => ">"
    } ;
}
