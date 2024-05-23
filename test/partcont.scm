;;
;; This file is included from continuation.scm
;;
;; We split this so that we can also run it with srfi-226 reference
;; implementation to compare behavior.  Be careful not to use Gauche's
;; extended syntax.

(test* "reset/shift combination 1"
       1000
       (begin
         (define k1 #f)
         (define k2 #f)
         (define k3 #f)
         (reset
          (shift k (set! k1 k)
                 (shift k (set! k2 k)
                        (shift k (set! k3 k))))
          1000)
         (k1)
         ;(k2)
         ;(k3)
         ))

(test* "reset/shift + values 1"
       '(1 2 3)
       (values->list (reset (values 1 2 3))))

(test* "reset/shift + values 2"
       '(1 2 3)
       (begin
         (define k1 #f)
         (reset
          (shift k (set! k1 k))
          (values 1 2 3))
         (values->list (k1))))

(test* "reset/shift + parameterize 1"
       "010"
       (with-output-to-string
         (lambda ()
           (define p (make-parameter 0))
           (display (p))
           (reset
            (parameterize ((p 1))
              (display (p))
              ;; expr of 'shift' is executed on the outside of 'reset'
              (shift k (display (p))))))))

(test* "reset/shift + call/cc 1"
       "[r01][r02][r02][r03]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define done #f)
           (call/cc
            (lambda (k0)
              (reset
               (display "[r01]")
               (shift k (set! k1 k))
               (display "[r02]")
               (unless done
                 (set! done #t)
                 (k0))
               (display "[r03]"))))
           (k1))))


(test* "reset/shift + call/cc 2"
       "[r01][s01][s02][s02]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (display "[r01]")
            (shift k (set! k1 k))
            (display "[s01]")
            (call/cc (lambda (k) (set! k2 k)))
            (display "[s02]"))
           (k1)
           (reset (reset (k2))))))

(test* "reset/shift + call/cc 2-B"
       "[r01][s01]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (display "[r01]")
            (shift k (set! k1 k))
            (display "[s01]")
            (call/cc (lambda (k) (set! k2 k)))
            ;; empty after call/cc
            ;(display "[s02]")
            )
           (k1)
           (reset (reset (k2))))))

(test* "reset/shift + call/cc 2-C"
       "[d01][d02][d03][d01][s01][s02][d03][d01][s02][d03]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda ()
               (display "[d02]")
               (shift k (set! k1 k))
               (display "[s01]")
               (call/cc (lambda (k) (set! k2 k)))
               (display "[s02]"))
             (lambda () (display "[d03]"))))
           (k1)
           (reset (reset (k2))))))

(test* "reset/shift + call/cc 2-D (from Kahua nqueen broken)"
       "[r01][s01][s02][d01][d02][d03][s02][d01]12345[d03]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (display "[r01]")
            (shift k (set! k1 k))
            (display "[s01]")
            (call/cc (lambda (k) (set! k2 k)))
            (display "[s02]")
            12345)
           (k1)
           (dynamic-wind
            (lambda () (display "[d01]"))
            (lambda () (display "[d02]")
                    (display (reset (reset (k2)))))
            (lambda () (display "[d03]"))))))

(test* "reset/shift + call/cc 3"
       "[r01][s01][s01]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (display "[r01]")
            (call/cc (lambda (k)
                       (set! k1 k)
                       (shift k (set! k2 k))))
            (display "[s01]"))
           (k2)
           (reset (k1)))))

(test* "reset/shift + call/cc error 1"
       (test-error)
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (define (f1) (call/cc (lambda (k) (set! k1 k)))
                        (shift k (set! k2 k))
                        (display "[f01]"))
           (define (f2) (display "[f02]"))
           (reset (f1) (f2))
           (reset (k1)))))

(test* "reset/shift + call/cc error 2"
       (test-error)
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (define k3 #f)
           (define (f1) (call/cc (lambda (k) (set! k1 k)))
                        (shift k (set! k2 k))
                        (display "[f01]"))
           (define (f2) (display "[f02]"))
           (reset (f1) (f2))
           (reset (shift k (set! k3 k)) (k1))
           (k3))))

(test* "reset/shift + call/cc error 3"
       (test-error)
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (call/cc (lambda (k) (set! k1 k)))
            (shift k (set! k2 k)))
           (k2)
           (k1))))

(let ((p (make-parameter 0))
      (c #f))
  (define (foo)
    (reset
     (display (p))
     (parameterize ((p 1))
       (let/cc cont
         (display (p))
         (shift k (display (p)) (cont k))
         (display (p))))))
  (test* "reset/shift + call/cc + parameterize" "010"
         (with-output-to-string
           (lambda () (set! c (foo)))))
  (test* "reset/shift + call/cc + parameterize" "1"
         (with-output-to-string c)))


(let ((p (make-parameter 1))
      (c #f))
  (define (foo)
    (parameterize ((p 2))
      (reset
       (display (p))
       (temporarily ((p 3))
         (display (p))
         (shift k (display (p)) (set! c k))
         (display (p)))
       (display (p)))))
  (test* "reset/shift + temporarily + parameterize" "232"
         (with-output-to-string foo))
  (test* "reset/shift + temporarily + parameterize (cont)" "32"
         (with-output-to-string c)))

(cond-expand
 (gauche
  (test* "reset/shift + with-error-handler 1"
         "[E01][E02]"
         (with-output-to-string
           (lambda ()
             (with-error-handler
                 (lambda (e) (display (~ e 'message)))
               (lambda ()
                 (display "[E01]")
                 (reset (error "[E02]"))
                 (display "[E03]"))))))

  (test* "reset/shift + guard 1"
         "[W01][D01][D02][W01][D01][D01][E01][D02][D02]"
         (with-output-to-string
           (lambda ()
             (define queue '())
             (define (yield) (shift k (push! queue k)))
             (push! queue (lambda ()
                            (guard (e (else (display (~ e 'message))))
                              (yield)
                              (error "[E01]"))))
             (while (and (pair? queue) (pop! queue))
               => next
               (display "[W01]")
               (reset
                (dynamic-wind
                  (lambda () (display "[D01]"))
                  next
                  (lambda () (display "[D02]"))))))))
  )
 (else))

(test* "dynamic-wind + reset/shift 1"
       "[d01][d02][d03][d04]"
       ;"[d01][d02][d04][d01][d03][d04]"
       (with-output-to-string
         (lambda ()
           (reset
            (shift
             k
             (dynamic-wind
              (lambda () (display "[d01]"))
              (lambda () (display "[d02]")
                   (k)
                   (display "[d03]"))
              (lambda () (display "[d04]"))))))))

(test* "dynamic-wind + reset/shift 2"
       "[d01][d02][d04][d01][d03][d04]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda ()
               (display "[d02]")
               (shift k (set! k1 k))
               (display "[d03]"))
             (lambda () (display "[d04]"))))
           (k1))))

(test* "dynamic-wind + reset/shift 3"
       "[d01][d02][d01][d02][d01][d02][d01][d02]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda ()
               (shift k (set! k1 k))
               (shift k (set! k2 k)))
             (lambda () (display "[d02]"))))
           (k1)
           (k2)
           (k2))))

(test* "dynamic-wind + reset/shift 3-B"
       "[d01][d02][d01][d11][d12][d02][d01][d11][d12][d02][d01][d11][d12][d02]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda () 
               (shift k (set! k1 k))
               (dynamic-wind
                 (lambda () (display "[d11]"))
                 (lambda () (shift k (set! k2 k)))
                 (lambda () (display "[d12]"))))
             (lambda () (display "[d02]"))))
           (k1)
           (k2)
           (k2))))

(test* "dynamic-wind + reset/shift 3-C"
       "[d01][d02][d21][d01][d11][d12][d02][d01][d11][d12][d02][d01][d11][d12][d02][d22]"
       ;"[d01][d02][d21][d22][d01][d11][d12][d02][d21][d22][d01][d11][d12][d02][d21][d22][d01][d11][d12][d02][d21][d22]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda () 
               (shift k (set! k1 k))
               (dynamic-wind
                 (lambda () (display "[d11]"))
                 (lambda () (shift k (set! k2 k)))
                 (lambda () (display "[d12]"))))
             (lambda () (display "[d02]"))))
           (dynamic-wind
            (lambda () (display "[d21]"))
            (lambda () (k1) (k2) (k2))
            (lambda () (display "[d22]"))))))

(test* "dynamic-wind + reset/shift 4"
       "[d01][d11][d12][d02][d11][d12]"
       ;"[d01][d11][d12][d02][d01][d11][d12][d02]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda ()
               (reset
                (dynamic-wind
                  (lambda () (display "[d11]"))
                  (lambda () (shift k (set! k1 k)))
                  (lambda () (display "[d12]")))))
             (lambda () (display "[d02]"))))
           (k1))))

(test* "dynamic-wind + reset/shift 5"
       "[d01][d02][d01][d11][d12][d02][d11][d12][d11][d12]"
       ;"[d01][d02][d01][d11][d12][d02][d01][d11][d12][d02][d01][d11][d12][d02]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (define k2 #f)
           (define k3 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda ()
               (shift k (set! k1 k))
               (reset
                (dynamic-wind
                  (lambda () (display "[d11]"))
                  (lambda ()
                    (shift k (set! k2 k))
                    (shift k (set! k3 k)))
                  (lambda () (display "[d12]")))))
             (lambda () (display "[d02]"))))
           (k1)
           (k2)
           (k3))))

(test* "dynamic-wind + reset/shift 6"
       "[d01][d02][d11][d12][d13][d14][d03][d04]"
       ;"[d01][d02][d11][d12][d14][d04][d01][d11][d13][d14][d03][d04]"
       (with-output-to-string
         (lambda ()
           (reset
            (shift
             k
             (dynamic-wind
              (lambda () (display "[d01]"))
              (lambda ()
                (display "[d02]")
                (dynamic-wind
                  (lambda () (display "[d11]"))
                  (lambda () 
                    (display "[d12]")
                    (k)
                    (display "[d13]"))
                  (lambda () (display "[d14]")))
                (display "[d03]"))
              (lambda () (display "[d04]"))))))))

(test* "dynamic-wind + reset/shift 7"
       "[d01][d02][d11][d12][d14][d04][d01][d11][d13][d14][d03][d04]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda ()
               (display "[d02]")
               (dynamic-wind
                 (lambda () (display "[d11]"))
                 (lambda () 
                   (display "[d12]")
                   (shift k (set! k1 k))
                   (display "[d13]"))
                 (lambda () (display "[d14]")))
               (display "[d03]"))
             (lambda () (display "[d04]"))))
           (k1))))

(test* "dynamic-wind + reset/shift 8"
       "[d01][d02][d04][d11][d12][d01][d03][d04][d13][d14]"
       ;"[d01][d02][d04][d11][d12][d14][d01][d03][d04][d11][d13][d14]"
       (with-output-to-string
         (lambda ()
           (define k1 #f)
           (reset
            (dynamic-wind
             (lambda () (display "[d01]"))
             (lambda () 
               (display "[d02]")
               (shift k (set! k1 k))
               (display "[d03]"))
             (lambda () (display "[d04]"))))
           (dynamic-wind
            (lambda () (display "[d11]"))
            (lambda () 
              (display "[d12]")
              (k1)
              (display "[d13]"))
            (lambda () (display "[d14]"))))))